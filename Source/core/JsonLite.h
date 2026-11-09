#pragma once

// A small JSON DOM for prototype saves and the golden fixtures (PLUGIN-PLAN 1.1, 5.1). Numbers are read
// with std::from_chars, so every number JSON.stringify wrote comes back as the same double, and written
// with jsNumberToString, so toJson gives what JSON.stringify gives for the same value. Objects keep their
// keys in order. Never used on the audio thread (it allocates).

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sl::core
{
    class JsonValue
    {
    public:
        enum class Type : std::uint8_t { null, boolean, number, string, array, object };

        JsonValue() noexcept = default;           // null
        JsonValue (std::nullptr_t) noexcept {}
        JsonValue (bool b) noexcept;
        JsonValue (int n) noexcept;
        JsonValue (double n) noexcept;
        JsonValue (const char* s);
        JsonValue (std::string s) noexcept;
        JsonValue (std::string_view s);

        static JsonValue array();
        static JsonValue object();

        Type type() const noexcept { return t; }
        bool isNull() const noexcept { return t == Type::null; }
        bool isBool() const noexcept { return t == Type::boolean; }
        bool isNumber() const noexcept { return t == Type::number; }
        bool isString() const noexcept { return t == Type::string; }
        bool isArray() const noexcept { return t == Type::array; }
        bool isObject() const noexcept { return t == Type::object; }

        /** The value when it has that type, else the fallback (or an empty string). */
        bool asBool (bool fallback = false) const noexcept;
        double asNumber (double fallback = 0.0) const noexcept;
        const std::string& asString() const noexcept;

        /** Elements of an array or members of an object; 0 for anything else. */
        std::size_t size() const noexcept;

        /** An array element; a shared null when out of range or not an array. */
        const JsonValue& operator[] (std::size_t index) const noexcept;
        const JsonValue& operator[] (int index) const noexcept;

        /** An object member; a shared null when missing or not an object. */
        const JsonValue& operator[] (std::string_view key) const noexcept;
        const JsonValue& operator[] (const char* key) const noexcept;

        /** The member, or nullptr when missing or not an object. */
        const JsonValue* find (std::string_view key) const noexcept;
        bool contains (std::string_view key) const noexcept { return find (key) != nullptr; }

        /** An object's i-th key and value, in the order they were set or read. */
        const std::string& keyAt (std::size_t i) const noexcept;
        const JsonValue& valueAt (std::size_t i) const noexcept;

        /** Appends to an array (a null becomes an empty array first). Returns the new element. */
        JsonValue& push (JsonValue value);

        /** Sets an object member (a null becomes an empty object first). An existing key keeps its place
            and takes the new value, as in JavaScript. Returns the member's value. */
        JsonValue& set (std::string_view key, JsonValue value);

    private:
        Type t = Type::null;
        bool boolValue = false;
        double numberValue = 0.0;
        std::string text;                  // a string's value
        std::vector<JsonValue> items;      // an array's elements, or an object's values
        std::vector<std::string> keys;     // an object's keys, parallel to items
    };

    struct JsonError
    {
        std::size_t offset = 0;   // byte offset into the text
        std::string message;
    };

    /** Parses one JSON text as JSON.parse accepts it (RFC 8259: no comments, no trailing commas, no NaN).
        Strings are stored as UTF-8; a lone surrogate escape ("\ud800") is kept as its 3-byte WTF-8
        form so toJson writes it back. Nesting is limited to kMaxJsonDepth. Returns false and fills error
        (when given) on bad input; out is then unspecified. */
    bool parseJson (std::string_view text, JsonValue& out, JsonError* error = nullptr);

    inline constexpr int kMaxJsonDepth = 256;

    /** JSON.stringify (value): no spaces, numbers as jsNumberToString, NaN and the infinities as null,
        strings escaped as JSON.stringify escapes them (quote, backslash, \b \f \n \r \t, other control
        characters and lone surrogates as lowercase \u00XX / \udXXX; everything else as it is). */
    std::string toJson (const JsonValue& value);
    void appendJson (std::string& out, const JsonValue& value);
}
