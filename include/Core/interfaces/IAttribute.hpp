#pragma once
#include <string>
#include <memory>
#include <format>
#include <string_view>
#include <concepts>
#include <ranges>
#include <algorithm> 
#include <vector>
#include <map>
// مفهوم (Concept) للسمات
template<typename T>
concept AttributeType = requires(T t, std::string_view sv) {
    { t.name() } -> std::convertible_to<std::string_view>;
    { t.value() } -> std::convertible_to<std::string_view>;
    { t.replace(sv) } -> std::same_as<void>;
    { t.clone() } -> std::convertible_to<std::unique_ptr<T>>;
    { t.to_xml() } -> std::same_as<std::string>;
    { t.to_json() } -> std::same_as<std::string>;
};

// الكلاس الأساسي باستخدام CRTP
template<typename Derived>
class IAttribute {
protected:
    std::string name_;
    
public:
    explicit IAttribute(std::string_view name) : name_(name) {}
    virtual ~IAttribute() = default;
    
    const std::string& name() const noexcept { return name_; }
    
    // واجهات必須 يتم تنفيذها في المشتقات
    std::string value() const { 
        return static_cast<const Derived*>(this)->value_impl(); 
    }
    
    void replace(std::string_view new_value) {
        static_cast<Derived*>(this)->replace_impl(new_value);
    }
    
    std::unique_ptr<Derived> clone() const {
        return static_cast<const Derived*>(this)->clone_impl();
    }
    
    std::string to_xml() const {
        return static_cast<const Derived*>(this)->to_xml_impl();
    }
    
    std::string to_json() const {
        return static_cast<const Derived*>(this)->to_json_impl();
    }
    
    std::string to_str(char separator = '=') const {
        return static_cast<const Derived*>(this)->to_str_impl(separator);
    }
};


class SingleAttribute final : public IAttribute<SingleAttribute> {
private:
    std::string value_;
    
public:
    SingleAttribute(std::string_view name, std::string_view value = "")
        : IAttribute(name), value_(value) {}
    
    // تنفيذ الواجهات
    std::string value_impl() const { return value_; }
    
    void replace_impl(std::string_view new_value) { 
        value_ = new_value; 
    }
    
    std::unique_ptr<SingleAttribute> clone_impl() const {
        return std::make_unique<SingleAttribute>(*this);
    }
    
    std::string to_xml_impl() const {
        return std::format("<{}>{}</{}>", name_, value_, name_);
    }
    
    std::string to_json_impl() const {
        return std::format("\"{}\": \"{}\"", name_, value_);
    }
    
    std::string to_str_impl(char separator) const {
        return std::format("{}{}{}", name_, separator, value_);
    }
    
    // مشغلات حديثة
    auto operator<=>(const SingleAttribute& other) const = default;
    
    // تحويل إلى string_view للكفاءة
    std::string_view value_view() const noexcept { return value_; }
    
    // فحص إذا كانت القيمة رقمية
    bool is_numeric() const noexcept {
        if (value_.empty()) return false;
        
        auto view = value_ | std::views::filter([](char c) { 
            return !std::isspace(static_cast<unsigned char>(c)); 
        });
        
        std::string trimmed(view.begin(), view.end());
        
        // استخدام std::from_chars في C++17+ أو التحقق اليدوي
        return !trimmed.empty() && 
               std::ranges::all_of(trimmed, [](char c) { 
                   return std::isdigit(static_cast<unsigned char>(c)); 
               });
    }
};

class VectorAttribute final : public IAttribute<VectorAttribute> {
private:
    std::vector<std::pair<std::string, std::string>> attributes_;
    
public:
    explicit VectorAttribute(std::string_view name) : IAttribute(name) {}
    
    // إضافة/استبدال سمات فرعية
    void add(std::string_view sub_name, std::string_view sub_value) {
        attributes_.emplace_back(sub_name, sub_value);
    }
    
    void replace(std::string_view sub_name, std::string_view new_value) {
        auto it = std::ranges::find_if(attributes_, 
            [&](const auto& pair) { return pair.first == sub_name; });
        
        if (it != attributes_.end()) {
            it->second = new_value;
        } else {
            add(sub_name, new_value);
        }
    }
    
    // الحصول على القيمة (ترجع قيمة افتراضية أو أول قيمة)
    std::string value_impl() const {
        return attributes_.empty() ? "" : attributes_.front().second;
    }
    
    void replace_impl(std::string_view new_value)  {
        if (!attributes_.empty()) {
            attributes_.front().second = new_value;
        }
    }
    
    std::unique_ptr<VectorAttribute> clone_impl() const {
        auto cloned = std::make_unique<VectorAttribute>(name_);
        cloned->attributes_ = attributes_;
        return cloned;
    }
    
    std::string to_xml_impl() const {
        std::string result = std::format("<{}>", name_);
        
        for (const auto& [key, value] : attributes_) {
            result += std::format("<{0}>{1}</{0}>", key, value);
        }
        
        result += std::format("</{}>", name_);
        return result;
    }
    
    std::string to_json_impl() const {
        std::string result = std::format("\"{}\": {{", name_);
        
        bool first = true;
        for (const auto& [key, value] : attributes_) {
            if (!first) result += ", ";
            result += std::format("\"{}\": \"{}\"", key, value);
            first = false;
        }
        
        result += "}";
        return result;
    }
    
    std::string to_str_impl(char separator) const {
        std::string result = std::format("{} = [", name_);
        
        for (const auto& [key, val] : attributes_) {
            result += std::format(" {}={},", key, val);
        }
        
        if (!attributes_.empty()) {
            result.pop_back(); // إزالة الفاصلة الأخيرة
        }
        
        result += " ]";
        return result;
    }
    
    // ميزات حديثة
    auto begin() const { return attributes_.begin(); }
    auto end() const { return attributes_.end(); }
    auto size() const { return attributes_.size(); }
    bool empty() const { return attributes_.empty(); }
    
    // البحث باستخدام ranges
    auto find(std::string_view sub_name) const {
        return std::ranges::find_if(attributes_,
            [&](const auto& pair) { return pair.first == sub_name; });
    }
};