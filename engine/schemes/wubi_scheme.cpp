#include "wubi_scheme.h"
#include <algorithm>
#include <cctype>

namespace
{
bool is_wubi_letter(char lower, bool mixed_pinyin_allowed, bool z_wildcard)
{
    // Both flags exist to let z in: mixed input because z is a pinyin letter, wildcard because
    // z stands for a code letter nobody knows. They are independent settings; accepting either
    // is the same one line either way.
    return (lower >= 'a' && lower <= 'y') || (lower == 'z' && (mixed_pinyin_allowed || z_wildcard));
}

bool is_wubi_vk(ImeKeyCode vk, bool mixed_pinyin_allowed, bool z_wildcard)
{
    return vk >= 'A' && vk <= 'Z' &&
           is_wubi_letter(static_cast<char>(vk + ('a' - 'A')), mixed_pinyin_allowed, z_wildcard);
}

// The raw string with cases is what a host reads back to recognise T mode (an uppercase T prefix)
// and V mode (an uppercase V followed by digits) from its shape. It keeps the case and the
// non-letter keys those modes type (digits, operators, ':' and '/'), but drops letters outside the
// code domain exactly as normalize_wubi_code does: with mixed input and z wildcard off, a typed z
// is not a code position, and keeping it would make the raw string longer than the code. The Server
// counts the raw string to decide when a four-letter code is complete, so a dropped letter must not
// count as one.
std::string normalize_wubi_with_cases(const std::string &input, bool mixed_pinyin_allowed, bool z_wildcard)
{
    std::string filtered;
    filtered.reserve(input.size());
    for (const unsigned char ch : input)
    {
        const bool ascii_letter = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
        if (ascii_letter && !is_wubi_letter(static_cast<char>(std::tolower(ch)), mixed_pinyin_allowed, z_wildcard))
        {
            continue;
        }
        filtered.push_back(static_cast<char>(ch));
    }
    return filtered;
}

std::string normalize_wubi_code(const std::string &input, size_t max_length, bool mixed_pinyin_allowed, bool z_wildcard)
{
    std::string normalized;
    normalized.reserve((std::min)(input.size(), max_length));
    for (const unsigned char ch : input)
    {
        const char lower = static_cast<char>(std::tolower(ch));
        if (!is_wubi_letter(lower, mixed_pinyin_allowed, z_wildcard))
        {
            continue;
        }
        normalized.push_back(lower);
        if (normalized.size() == max_length)
        {
            break;
        }
    }
    return normalized;
}
} // namespace

void WubiScheme::reset()
{
    raw_input_with_cases_.clear();
    raw_input_.clear();
    key_strokes_.clear();
    extended_length_allowed_ = false;
}

void WubiScheme::handle_key(ImeKeyCode vk, ImeModifierMask modifiers_down, ImeCharacter wch)
{
    if (vk == ImeKey::Backspace)
    {
        if (!raw_input_with_cases_.empty())
        {
            raw_input_with_cases_.pop_back();
        }
        if (!raw_input_.empty())
        {
            raw_input_.pop_back();
        }
        if (!key_strokes_.empty())
        {
            key_strokes_.pop_back();
        }
        return;
    }

    if (vk == ImeKey::Escape || vk == ImeKey::Return)
    {
        reset();
        return;
    }

    if (!is_wubi_vk(vk, mixed_pinyin_allowed_, z_wildcard_) || raw_input_.size() >= max_code_length())
    {
        return;
    }

    key_strokes_.push_back(KeyStroke{vk, modifiers_down, wch});
    raw_input_.push_back(static_cast<char>(vk + ('a' - 'A')));
    // Same case rule as quanpin: the character the key produced when there is one, otherwise the
    // key's own letter. Both fields stay in step, so the raw string is always the code as typed.
    if (wch >= L'A' && wch <= L'Z')
    {
        raw_input_with_cases_.push_back(static_cast<char>(wch));
    }
    else if (wch >= L'a' && wch <= L'z')
    {
        raw_input_with_cases_.push_back(static_cast<char>(wch));
    }
    else
    {
        raw_input_with_cases_.push_back(static_cast<char>(vk + ('a' - 'A')));
    }
}

void WubiScheme::set_raw_input(const std::string &raw_input, const std::string &raw_input_with_cases)
{
    raw_input_with_cases_ = normalize_wubi_with_cases(raw_input_with_cases.empty() ? raw_input : raw_input_with_cases,
                                                      mixed_pinyin_allowed_, z_wildcard_);
    raw_input_ = normalize_wubi_code(raw_input_with_cases_, max_code_length(), mixed_pinyin_allowed_, z_wildcard_);
    key_strokes_.clear();
}

void WubiScheme::set_extended_length_allowed(bool allowed)
{
    extended_length_allowed_ = allowed;
}

void WubiScheme::set_mixed_pinyin_allowed(bool allowed)
{
    mixed_pinyin_allowed_ = allowed;
}

void WubiScheme::set_z_wildcard(bool enabled)
{
    z_wildcard_ = enabled;
}

size_t WubiScheme::max_code_length() const
{
    return extended_length_allowed_ ? kMaxMixedCodeLength : kMaxCodeLength;
}

QueryRequest WubiScheme::build_request() const
{
    QueryRequest request;
    request.scheme = type();
    request.raw_input = raw_input_;
    request.raw_input_with_cases = raw_input_with_cases_;
    request.normalized_input = raw_input_;
    request.raw_segmentation = raw_input_;
    request.normalized_segmentation = raw_input_;
    request.segmentation = raw_input_;
    request.key_strokes = key_strokes_;
    // Wildcard mode only: the provider switches from a range scan to a pattern match when the
    // code actually holds a z. Set here rather than in the provider so mixed input alone never
    // reaches the pattern path; with both on, the session ranks pinyin ahead of the wildcard rows.
    request.wubi_z_wildcard = z_wildcard_ && raw_input_.find('z') != std::string::npos;
    request.valid = !raw_input_.empty();
    return request;
}

std::string WubiScheme::get_preedit() const
{
    return raw_input_;
}

SchemeType WubiScheme::type() const
{
    return SchemeType::Wubi;
}
