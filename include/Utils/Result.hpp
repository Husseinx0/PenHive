#pragma once
#include <stdexcept>
#include <string>
#include <variant>

template <typename T, typename E = std::string>
class Result {
  std::variant<T, E> storage;

public:
  Result(T&& value)
      : storage(std::move(value)) {}
  Result(E&& error)
      : storage(std::move(error)) {}

  [[nodiscard]] bool isOk() const noexcept { 
    return std::holds_alternative<T>(storage); 
  }
  [[nodiscard]] bool isErr() const noexcept { return std::holds_alternative<E>(storage); }

  T expect(const std::string& msg) {
    if (isErr()) throw std::runtime_error(msg + ": " + std::get<E>(storage));
    return std::get<T>(storage);
  }

  T unwrap() { return expect("Called unwrap on error Result"); }
  E unwrapErr() {
    if (isOk()) throw std::runtime_error("Called unwrapErr on ok Result");
    return std::get<E>(storage);
  }

  T unwrapOr(T&& defaultValue) { return isOk() ? std::get<T>(storage) : std::move(defaultValue); }
};