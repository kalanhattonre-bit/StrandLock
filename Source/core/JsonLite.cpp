#include "JsonLite.h"

#include "JsCompat.h"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <system_error>
#include <utility>

namespace sl::core
{
    namespace
    {
        const JsonValue& sharedNull() noexcept
        {
            static const JsonValue null;
            return null;
        }

        const std::string& sharedEmptyString() noexcept
        {
            static const std::string empty;
            return empty;
        }
    }

    //==============================================================================================
    JsonValue::JsonValue (bool b) noexcept : t (Type::boolean), boolValue (b) {}
    JsonValue::JsonValue (int n) noexcept : t (Type::number), numberValue (static_cast<double> (n)) {}
    JsonValue::JsonValue (double n) noexcept : t (Type::number), numberValue (n) {}
    JsonValue::JsonValue (const char* s) : t (Type::string), text (s != nullptr ? s : "") {}
    JsonValue::JsonValue (std::string s) noexcept : t (Type::string), text (std::move (s)) {}
    JsonValue::JsonValue (std::string_view s) : t (Type::string), text (s) {}

    JsonValue JsonValue::array()
    {
        JsonValue v;
        v.t = Type::array;
        return v;
    }

    JsonValue JsonValue::object()
    {
        JsonValue v;
        v.t = Type::object;
        return v;
    }

    bool JsonValue::asBool (bool fallback) const noexcept
    {
        return t == Type::boolean ? boolValue : fallback;
    }

    double JsonValue::asNumber (double fallback) const noexcept
    {
        return t == Type::number ? numberValue : fallback;
    }

    const std::string& JsonValue::asString() const noexcept
    {
        return t == Type::string ? text : sharedEmptyString();
    }

    std::size_t JsonValue::size() const noexcept
    {
        return t == Type::array || t == Type::object ? items.size() : 0;
    }

    const JsonValue& JsonValue::operator[] (std::size_t index) const noexcept
    {
        return t == Type::array && index < items.size() ? items[index] : sharedNull();
    }

    const JsonValue& JsonValue::operator[] (int index) const noexcept
    {
        return index < 0 ? sharedNull() : (*this)[static_cast<std::size_t> (index)];
    }

    const JsonValue& JsonValue::operator[] (std::string_view key) const noexcept
    {
        const JsonValue* v = find (key);
        return v != nullptr ? *v : sharedNull();
    }

    const JsonValue& JsonValue::operator[] (const char* key) const noexcept
    {
        return (*this)[std::string_view (key != nullptr ? key : "")];
    }

    const JsonValue* JsonValue::find (std::string_view key) const noexcept
    {
        if (t != Type::object)
            return nullptr;
        for (std::size_t i = 0; i < keys.size(); ++i)
            if (keys[i] == key)
                return &items[i];
        return nullptr;
    }

    const std::string& JsonValue::keyAt (std::size_t i) const noexcept
    {
        return t == Type::object && i < keys.size() ? keys[i] : sharedEmptyString();
    }

    const JsonValue& JsonValue::valueAt (std::size_t i) const noexcept
    {
        return t == Type::object && i < items.size() ? items[i] : sharedNull();
    }

    JsonValue& JsonValue::push (JsonValue value)
    {
        if (t != Type::array)
            *this = array();
        items.push_back (std::move (value));
        return items.back();
    }

    JsonValue& JsonValue::set (std::string_view key, JsonValue value)
    {
        if (t != Type::object)
            *this = object();
        for (std::size_t i = 0; i < keys.size(); ++i)
        {
            if (keys[i] == key)
            {
                items[i] = std::move (value);
                return items[i];
            }
        }
        keys.emplace_back (key);
        items.push_back (std::move (value));
        return items.back();
    }

    //==============================================================================================
    namespace
    {
        void appendUtf8 (std::string& out, std::uint32_t cp)
        {
            // Surrogates (a lone "\ud800") go out as their 3-byte form, WTF-8, so they survive a round trip.
            if (cp < 0x80u)
            {
                out += static_cast<char> (cp);
            }
            else if (cp < 0x800u)
            {
                out += static_cast<char> (0xc0u | (cp >> 6));
                out += static_cast<char> (0x80u | (cp & 0x3fu));
            }
            else if (cp < 0x10000u)
            {
                out += static_cast<char> (0xe0u | (cp >> 12));
                out += static_cast<char> (0x80u | ((cp >> 6) & 0x3fu));
                out += static_cast<char> (0x80u | (cp & 0x3fu));
            }
            else
            {
                out += static_cast<char> (0xf0u | (cp >> 18));
                out += static_cast<char> (0x80u | ((cp >> 12) & 0x3fu));
                out += static_cast<char> (0x80u | ((cp >> 6) & 0x3fu));
                out += static_cast<char> (0x80u | (cp & 0x3fu));
            }
        }

        struct Parser
        {
            std::string_view in;
            std::size_t pos = 0;
            JsonError* error = nullptr;

            bool fail (const char* message)
            {
                if (error != nullptr && error->message.empty())
                {
                    error->offset = pos;
                    error->message = message;
                }
                return false;
            }

            bool atEnd() const noexcept { return pos >= in.size(); }
            char peek() const noexcept { return pos < in.size() ? in[pos] : '\0'; }

            void skipSpace() noexcept
            {
                while (pos < in.size() && (in[pos] == ' ' || in[pos] == '\t' || in[pos] == '\n' || in[pos] == '\r'))
                    ++pos;
            }

            bool literal (std::string_view word)
            {
                if (in.substr (pos, word.size()) != word)
                    return fail ("unknown literal");
                pos += word.size();
                return true;
            }

            static bool isDigit (char c) noexcept { return c >= '0' && c <= '9'; }

            bool hex4 (std::uint32_t& out)
            {
                if (pos + 4 > in.size())
                    return fail ("short \\u escape");
                out = 0;
                for (int i = 0; i < 4; ++i)
                {
                    const char c = in[pos++];
                    std::uint32_t v = 0;
                    if (c >= '0' && c <= '9')       v = static_cast<std::uint32_t> (c - '0');
                    else if (c >= 'a' && c <= 'f')  v = static_cast<std::uint32_t> (c - 'a' + 10);
                    else if (c >= 'A' && c <= 'F')  v = static_cast<std::uint32_t> (c - 'A' + 10);
                    else                            return fail ("bad \\u escape");
                    out = (out << 4) | v;
                }
                return true;
            }

            bool string (std::string& out)
            {
                ++pos;   // the opening quote
                for (;;)
                {
                    // Copy the plain run in one go.
                    const std::size_t start = pos;
                    while (pos < in.size())
                    {
                        const auto c = static_cast<unsigned char> (in[pos]);
                        if (c == '"' || c == '\\' || c < 0x20)
                            break;
                        ++pos;
                    }
                    out.append (in.data() + start, pos - start);

                    if (atEnd())
                        return fail ("unterminated string");

                    const char c = in[pos];
                    if (c == '"')
                    {
                        ++pos;
                        return true;
                    }
                    if (c != '\\')
                        return fail ("control character in a string");

                    ++pos;
                    if (atEnd())
                        return fail ("unterminated escape");
                    const char e = in[pos++];
                    switch (e)
                    {
                        case '"':  out += '"'; break;
                        case '\\': out += '\\'; break;
                        case '/':  out += '/'; break;
                        case 'b':  out += '\b'; break;
                        case 'f':  out += '\f'; break;
                        case 'n':  out += '\n'; break;
                        case 'r':  out += '\r'; break;
                        case 't':  out += '\t'; break;
                        case 'u':
                        {
                            std::uint32_t unit = 0;
                            if (! hex4 (unit))
                                return false;
                            if (unit >= 0xd800u && unit <= 0xdbffu && in.substr (pos, 2) == "\\u")
                            {
                                const std::size_t back = pos;
                                pos += 2;
                                std::uint32_t low = 0;
                                if (! hex4 (low))
                                    return false;
                                if (low >= 0xdc00u && low <= 0xdfffu)
                                {
                                    appendUtf8 (out, 0x10000u + ((unit - 0xd800u) << 10) + (low - 0xdc00u));
                                    break;
                                }
                                pos = back;   // not a pair: the high surrogate stands alone
                            }
                            appendUtf8 (out, unit);
                            break;
                        }
                        default:
                            return fail ("bad escape");
                    }
                }
            }

            bool number (double& out)
            {
                const std::size_t start = pos;
                if (peek() == '-')
                    ++pos;
                if (peek() == '0')
                {
                    ++pos;
                }
                else if (isDigit (peek()))
                {
                    while (isDigit (peek()))
                        ++pos;
                }
                else
                {
                    return fail ("bad number");
                }

                if (peek() == '.')
                {
                    ++pos;
                    if (! isDigit (peek()))
                        return fail ("bad number: no digit after the point");
                    while (isDigit (peek()))
                        ++pos;
                }

                if (peek() == 'e' || peek() == 'E')
                {
                    ++pos;
                    if (peek() == '+' || peek() == '-')
                        ++pos;
                    if (! isDigit (peek()))
                        return fail ("bad number: no exponent digits");
                    while (isDigit (peek()))
                        ++pos;
                }

                const char* first = in.data() + start;
                const char* last = in.data() + pos;
                const auto result = std::from_chars (first, last, out);
                if (result.ec == std::errc() && result.ptr == last)
                    return true;

                // Out of range (1e400, 1e-400): what JSON.parse gives, an infinity or zero.
                const std::string copy (first, last);
                out = std::strtod (copy.c_str(), nullptr);
                return true;
            }

            bool value (JsonValue& out, int depth)
            {
                if (depth > kMaxJsonDepth)
                    return fail ("nested too deeply");

                skipSpace();
                if (atEnd())
                    return fail ("unexpected end");

                const char c = peek();
                if (c == '{')
                {
                    ++pos;
                    out = JsonValue::object();
                    skipSpace();
                    if (peek() == '}')
                    {
                        ++pos;
                        return true;
                    }
                    for (;;)
                    {
                        skipSpace();
                        if (peek() != '"')
                            return fail ("expected a key");
                        std::string key;
                        if (! string (key))
                            return false;
                        skipSpace();
                        if (peek() != ':')
                            return fail ("expected ':'");
                        ++pos;
                        JsonValue member;
                        if (! value (member, depth + 1))
                            return false;
                        out.set (key, std::move (member));
                        skipSpace();
                        if (peek() == ',')
                        {
                            ++pos;
                            continue;
                        }
                        if (peek() == '}')
                        {
                            ++pos;
                            return true;
                        }
                        return fail ("expected ',' or '}'");
                    }
                }

                if (c == '[')
                {
                    ++pos;
                    out = JsonValue::array();
                    skipSpace();
                    if (peek() == ']')
                    {
                        ++pos;
                        return true;
                    }
                    for (;;)
                    {
                        JsonValue element;
                        if (! value (element, depth + 1))
                            return false;
                        out.push (std::move (element));
                        skipSpace();
                        if (peek() == ',')
                        {
                            ++pos;
                            continue;
                        }
                        if (peek() == ']')
                        {
                            ++pos;
                            return true;
                        }
                        return fail ("expected ',' or ']'");
                    }
                }

                if (c == '"')
                {
                    std::string s;
                    if (! string (s))
                        return false;
                    out = JsonValue (std::move (s));
                    return true;
                }

                if (c == 't')
                {
                    if (! literal ("true"))
                        return false;
                    out = JsonValue (true);
                    return true;
                }

                if (c == 'f')
                {
                    if (! literal ("false"))
                        return false;
                    out = JsonValue (false);
                    return true;
                }

                if (c == 'n')
                {
                    if (! literal ("null"))
                        return false;
                    out = JsonValue();
                    return true;
                }

                double n = 0.0;
                if (! number (n))
                    return false;
                out = JsonValue (n);
                return true;
            }
        };

        void appendQuoted (std::string& out, const std::string& s)
        {
            static constexpr char hexDigits[] = "0123456789abcdef";
            auto escapeUnit = [&out] (std::uint32_t unit)
            {
                out += "\\u";
                out += hexDigits[(unit >> 12) & 0xfu];
                out += hexDigits[(unit >> 8) & 0xfu];
                out += hexDigits[(unit >> 4) & 0xfu];
                out += hexDigits[unit & 0xfu];
            };

            out += '"';
            const std::size_t size = s.size();
            for (std::size_t i = 0; i < size; ++i)
            {
                const auto c = static_cast<unsigned char> (s[i]);
                switch (c)
                {
                    case '"':  out += "\\\""; continue;
                    case '\\': out += "\\\\"; continue;
                    case '\b': out += "\\b"; continue;
                    case '\f': out += "\\f"; continue;
                    case '\n': out += "\\n"; continue;
                    case '\r': out += "\\r"; continue;
                    case '\t': out += "\\t"; continue;
                    default: break;
                }

                if (c < 0x20u)
                {
                    escapeUnit (c);
                    continue;
                }

                // A surrogate stored as WTF-8 (ED A0..BF xx) was a lone one: JSON.stringify writes \udXXX.
                if (c == 0xedu && i + 2 < size)
                {
                    const auto c1 = static_cast<unsigned char> (s[i + 1]);
                    const auto c2 = static_cast<unsigned char> (s[i + 2]);
                    if (c1 >= 0xa0u && c1 <= 0xbfu && (c2 & 0xc0u) == 0x80u)
                    {
                        escapeUnit (0xd000u | ((c1 & 0x3fu) << 6) | (c2 & 0x3fu));
                        i += 2;
                        continue;
                    }
                }

                out += static_cast<char> (c);
            }
            out += '"';
        }
    }

    bool parseJson (std::string_view text, JsonValue& out, JsonError* error)
    {
        Parser p { text, 0, error };
        if (! p.value (out, 0))
            return false;
        p.skipSpace();
        if (! p.atEnd())
            return p.fail ("text after the value");
        return true;
    }

    void appendJson (std::string& out, const JsonValue& value)
    {
        switch (value.type())
        {
            case JsonValue::Type::null:
                out += "null";
                return;

            case JsonValue::Type::boolean:
                out += value.asBool() ? "true" : "false";
                return;

            case JsonValue::Type::number:
            {
                const double n = value.asNumber();
                out += std::isfinite (n) ? jsNumberToString (n) : std::string ("null");
                return;
            }

            case JsonValue::Type::string:
                appendQuoted (out, value.asString());
                return;

            case JsonValue::Type::array:
                out += '[';
                for (std::size_t i = 0; i < value.size(); ++i)
                {
                    if (i > 0)
                        out += ',';
                    appendJson (out, value[i]);
                }
                out += ']';
                return;

            case JsonValue::Type::object:
                out += '{';
                for (std::size_t i = 0; i < value.size(); ++i)
                {
                    if (i > 0)
                        out += ',';
                    appendQuoted (out, value.keyAt (i));
                    out += ':';
                    appendJson (out, value.valueAt (i));
                }
                out += '}';
                return;
        }
    }

    std::string toJson (const JsonValue& value)
    {
        std::string out;
        appendJson (out, value);
        return out;
    }
}
