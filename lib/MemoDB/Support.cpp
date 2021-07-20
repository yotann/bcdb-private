#include "memodb/Support.h"

#include <cstdint>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Error.h>
#include <optional>
#include <string>

using namespace memodb;
using llvm::StringRef;

std::string memodb::bytesToUTF8(llvm::ArrayRef<std::uint8_t> Bytes) {
  std::string Result;
  for (std::uint8_t Byte : Bytes) {
    if (Byte < 0x80) {
      Result.push_back(static_cast<char>(Byte));
    } else {
      Result.push_back(static_cast<char>(0xc0 | (Byte >> 6)));
      Result.push_back(static_cast<char>(0x80 | (Byte & 0x3f)));
    }
  }
  return Result;
}

std::string memodb::bytesToUTF8(StringRef Bytes) {
  return bytesToUTF8(llvm::ArrayRef(
      reinterpret_cast<const std::uint8_t *>(Bytes.data()), Bytes.size()));
}

std::string memodb::utf8ToByteString(StringRef Str) {
  std::string Result;
  while (!Str.empty()) {
    std::uint8_t x = (std::uint8_t)Str[0];
    if (x < 0x80) {
      Result.push_back(static_cast<char>(x));
      Str = Str.drop_front(1);
    } else {
      std::uint8_t y = Str.size() >= 2 ? (std::uint8_t)Str[1] : 0;
      if ((x & 0xfc) != 0xc0 || (y & 0xc0) != 0x80)
        llvm::report_fatal_error("invalid UTF-8 bytes");
      Result.push_back(static_cast<char>((x & 3) << 6 | (y & 0x3f)));
      Str = Str.drop_front(2);
    }
  }
  return Result;
}

std::optional<URI> URI::parse(StringRef str, bool allow_dot_segments) {
  URI uri;
  StringRef scheme_ref, authority_ref, host_ref, port_ref, path_ref, query_ref,
      fragment_ref;

  if (str.contains(':')) {
    std::tie(scheme_ref, str) = str.split(':');
    uri.scheme = scheme_ref.lower();
  }

  if (str.startswith("//")) {
    size_t i = str.find_first_of("/?#", 2);
    if (i == StringRef::npos) {
      authority_ref = str.substr(2);
      str = "";
    } else {
      authority_ref = str.substr(2, i - 2);
      str = str.substr(i);
    }
    if (authority_ref.contains('@'))
      return std::nullopt; // userinfo is not supported
    if (authority_ref.startswith("[")) {
      size_t j = authority_ref.find(']');
      if (j == StringRef::npos)
        return std::nullopt;
      host_ref = authority_ref.take_front(j + 1);
      port_ref = authority_ref.substr(j + 1);
      if (!port_ref.empty() && !port_ref.startswith(":"))
        return std::nullopt;
      port_ref = port_ref.drop_front();
    } else {
      std::tie(host_ref, port_ref) = authority_ref.split(':');
    }
  }

  std::tie(str, fragment_ref) = str.split('#');
  std::tie(path_ref, query_ref) = str.split('?');

  bool percent_decoding_error = false;

  auto percentDecode = [&percent_decoding_error](StringRef str) -> std::string {
    if (!str.contains('%'))
      return str.str();
    std::string result;
    while (!str.empty()) {
      size_t i = str.find('%');
      result.append(str.take_front(i));
      str = str.substr(i);
      if (str.empty())
        break;
      unsigned code;
      if (str.size() >= 3 && !str.substr(1, 2).getAsInteger(16, code)) {
        result.push_back(static_cast<char>(code));
        str = str.drop_front(3);
      } else {
        percent_decoding_error = true;
        break;
      }
    }
    return result;
  };

  uri.host = StringRef(percentDecode(host_ref)).lower();
  if (!port_ref.empty() && port_ref.getAsInteger(10, uri.port))
    return std::nullopt;
  uri.fragment = percentDecode(fragment_ref);

  uri.rootless = true;
  if (!path_ref.empty()) {
    if (path_ref.startswith("/")) {
      uri.rootless = false;
      path_ref = path_ref.drop_front();
    }
    llvm::SmallVector<StringRef, 8> segments;
    path_ref.split(segments, '/');
    for (const auto &segment : segments) {
      auto decoded = percentDecode(segment);
      if (!allow_dot_segments && (decoded == "." || decoded == ".."))
        return std::nullopt;
      uri.path_segments.emplace_back(std::move(decoded));
    }
  }

  if (!query_ref.empty()) {
    llvm::SmallVector<StringRef, 8> params;
    query_ref.split(params, '&');
    for (const auto &param : params)
      uri.query_params.emplace_back(percentDecode(param));
  }

  if (percent_decoding_error)
    return std::nullopt;
  return uri;
}

std::optional<std::string> URI::getPathString() const {
  std::string result;
  for (const auto &segment : path_segments) {
    if (StringRef(segment).contains('/'))
      return std::nullopt;
    result += "/" + segment;
  }
  return result;
}

std::string URI::encode() const {
  static const StringRef hex_digits = "0123456789ABCDEF";
  static const StringRef host_allowed =
      "!$&'()*+,-.0123456789:;=ABCDEFGHIJKLMNOPQRSTUVWXYZ[]_"
      "abcdefghijklmnopqrstuvvwxyz~";
  static const StringRef path_allowed =
      "!$&'()*+,-.0123456789:;=@ABCDEFGHIJKLMNOPQRSTUVWXYZ_"
      "abcdefghijklmnopqrstuvwxyz~";
  static const StringRef query_allowed =
      "!$'()*+,-./"
      "0123456789:;=?@ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz~";
  static const StringRef fragment_allowed =
      "!$&'()*+,-./"
      "0123456789:;=?@ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz~";
  std::string result;
  auto percentEncode = [&result](StringRef str, StringRef allowed) {
    while (!str.empty()) {
      size_t i = str.find_first_not_of(allowed);
      result += str.take_front(i);
      str = str.substr(i);
      if (str.empty())
        break;
      std::uint8_t c = str[0];
      result.push_back('%');
      result.push_back(hex_digits[c >> 4]);
      result.push_back(hex_digits[c & 0xf]);
      str = str.drop_front();
    }
  };

  if (!scheme.empty())
    result += StringRef(scheme).lower() + ":";
  if (!host.empty() || port != 0) {
    result += "//";
    percentEncode(StringRef(host).lower(), host_allowed);
    if (port != 0) {
      result += ":";
      result += llvm::Twine(port).str();
    }
  }
  if (!rootless)
    result += "/";
  if (!path_segments.empty()) {
    for (const auto &segment : path_segments) {
      percentEncode(segment, path_allowed);
      result += "/";
    }
    result.pop_back();
  }
  if (!query_params.empty()) {
    result += "?";
    for (const auto &param : query_params) {
      percentEncode(param, query_allowed);
      result += "&";
    }
    result.pop_back();
  }
  if (!fragment.empty()) {
    result += "#";
    percentEncode(fragment, fragment_allowed);
  }
  return result;
}
