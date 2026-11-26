#pragma once
// A gatekeeper for saved projects: is this text laid out the way the plugin's own projects are? Only then is it handed to
// JUCE's XML reader. Pure C++ (no JUCE) so it can be unit-tested anywhere.
//
// Why: JUCE 8.0.6's reader can read past the end of damaged text (for example an unfinished "&#x" right at the end, or a
// byte that announces a longer character there). Chasing bad cases one at a time would miss some, so this accepts only
// the small part of XML that AudioProcessor::copyXmlToBinary writes for a settings tree, and requires it to be complete:
//   - plain ASCII (JUCE writes every other character as a numbered reference)
//   - optionally its fixed <?xml ...?> header
//   - one element, with nested elements, each properly closed
//   - attributes written name="value", separated by white space
//   - references only as &amp; &lt; &gt; &quot; &apos; or &#123;
// No comments, CDATA, DOCTYPE or text between elements: the plugin never writes any.
#include <cstddef>
#include <cstring>

namespace savedtext
{
    constexpr int maxDepth = 8; // the plugin nests two deep

    namespace detail
    {
        struct Reader
        {
            const char* at;
            const char* end;

            char peek(std::size_t ahead = 0) const { return (std::size_t) (end - at) > ahead ? at[ahead] : '\0'; }
            bool take(char c) { if (peek() != c) return false; ++at; return true; }
            bool take(const char* word)
            {
                const auto n = std::strlen(word);
                if ((std::size_t) (end - at) < n || std::memcmp(at, word, n) != 0) return false;
                at += n;
                return true;
            }
            static bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
            static bool isLetter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == ':'; }
            static bool isDigit(char c) { return c >= '0' && c <= '9'; }
            bool spaces() { const auto* from = at; while (isSpace(peek())) ++at; return at != from; }

            bool name(const char*& first, std::size_t& length)
            {
                first = at;
                if (! isLetter(peek())) return false;
                while (isLetter(peek()) || isDigit(peek()) || peek() == '.' || peek() == '-') ++at;
                length = (std::size_t) (at - first);
                return true;
            }
            bool reference() // the '&' has been taken
            {
                if (take("amp;") || take("lt;") || take("gt;") || take("quot;") || take("apos;")) return true;
                if (! take('#')) return false;
                long code = 0;
                int digits = 0;
                while (isDigit(peek()) && digits < 8) { code = code * 10 + (peek() - '0'); ++at; ++digits; }
                return digits > 0 && digits < 8 && code > 0 && code <= 0x10ffff && ! (code >= 0xd800 && code <= 0xdfff) && take(';');
            }
            bool value()
            {
                if (! take('"')) return false;
                while (at < end)
                {
                    const char c = *at++;
                    if (c == '"') return true;
                    if (c == '<' || (c == '&' && ! reference()) || (c != '&' && c < ' ' && ! isSpace(c))) return false;
                }
                return false;
            }
            bool element(int depth)
            {
                const char* tag = nullptr;
                std::size_t tagLength = 0;
                if (depth > maxDepth || ! take('<') || ! name(tag, tagLength)) return false;
                for (;;)
                {
                    const bool spaced = spaces();
                    if (take("/>")) return true;
                    if (take('>')) break;
                    const char* attribute = nullptr;
                    std::size_t attributeLength = 0;
                    if (! spaced || ! name(attribute, attributeLength)) return false;
                    spaces();
                    if (! take('=')) return false;
                    spaces();
                    if (! value()) return false;
                }
                for (;;)
                {
                    spaces();
                    if (take("</"))
                    {
                        const char* closing = nullptr;
                        std::size_t closingLength = 0;
                        if (! name(closing, closingLength) || closingLength != tagLength || std::memcmp(closing, tag, tagLength) != 0) return false;
                        spaces();
                        return take('>');
                    }
                    if (! element(depth + 1)) return false;
                }
            }
        };
    }

    // `text` need not end with a zero; if it holds one, the text ends there (as it does for JUCE).
    inline bool isSavedProject(const char* text, std::size_t length)
    {
        if (text == nullptr) return false;
        if (const auto* zero = static_cast<const char*>(std::memchr(text, 0, length))) length = (std::size_t) (zero - text);
        for (std::size_t i = 0; i < length; ++i)
            if ((unsigned char) text[i] > 126) return false;

        detail::Reader r { text, text + length };
        r.spaces();
        r.take("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
        r.spaces();
        if (! r.element(1)) return false;
        r.spaces();
        return r.at == r.end;
    }
}
