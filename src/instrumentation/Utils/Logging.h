

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/raw_ostream.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <sys/types.h>

namespace __xsan {
using namespace llvm;

using ColorCode = raw_ostream::Colors;
struct LogColor {
  static constexpr ColorCode BLACK = raw_ostream::BLACK;
  static constexpr ColorCode RED = raw_ostream::RED;
  static constexpr ColorCode GREEN = raw_ostream::GREEN;
  static constexpr ColorCode YELLOW = raw_ostream::YELLOW;
  static constexpr ColorCode BLUE = raw_ostream::BLUE;
  static constexpr ColorCode MAGENTA = raw_ostream::MAGENTA;
  static constexpr ColorCode CYAN = raw_ostream::CYAN;
  static constexpr ColorCode WHITE = raw_ostream::WHITE;
  static constexpr ColorCode SAVEDCOLOR = raw_ostream::SAVEDCOLOR;
  static constexpr ColorCode RESET = raw_ostream::RESET;
};

class Logger {
private:
  class ScopeLog {
    friend class Logger;
    ScopeLog(Logger &, StringRef Title);

  public:
    ~ScopeLog();

  private:
    StringRef Title;
    Logger &Log;
  };

public:
  Logger() : Logger(errs()) {}
  Logger(raw_fd_ostream &os) : Os(os), IsBg(false), IsBold(false) {}

  ScopeLog scope(StringRef Title = "") { return ScopeLog(*this, Title); }
  Logger &delimiter(char c = '-', uint8_t width = 20, StringRef ID = "");

  Logger &padToColumn(uint16_t Column) {
    Os.PadToColumn(Column);
    return *this;
  }

  /// Similar to Python and Rust's format string syntax.
  template <typename... Ts> Logger &log(const char *s, Ts &&...args) {
    Os << formatv(s, std::forward<Ts>(args)...);
    return *this;
  }

  template <typename Ty> Logger &log(const Ty &s) {
    Os << s;
    return *this;
  }

  Logger &endl() {
    Os << "\n";
    return *this;
  }

  Logger &bold() {
    IsBold = true;
    return setBoldBG();
  };
  Logger &unbold() {
    IsBold = false;
    return setBoldBG();
  };
  Logger &foreground() {
    IsBg = false;
    return setBoldBG();
  };
  Logger &background() {
    IsBg = true;
    return setBoldBG();
  };

  Logger &setColor(ColorCode Color) {
    Os.changeColor(Color, IsBold, IsBg);
    CurColor = Color;
    return *this;
  };
  Logger &black() { return setColor(LogColor::BLACK); };
  Logger &blue() { return setColor(LogColor::BLUE); };
  Logger &yellow() { return setColor(LogColor::YELLOW); };
  Logger &red() { return setColor(LogColor::RED); };
  Logger &magenta() { return setColor(LogColor::MAGENTA); };
  Logger &cyan() { return setColor(LogColor::CYAN); };
  Logger &white() { return setColor(LogColor::WHITE); };
  Logger &green() { return setColor(LogColor::GREEN); };
  Logger &resetColor() { return setColor(LogColor::RESET); };
  Logger &reset() {
    unbold();
    foreground();
    Os.resetColor();
    return *this;
  };

  template <typename Ts> Logger &operator<<(const Ts &s) {
    Os << s;
    return *this;
  }
  void flush() { Os.flush(); }

private:
  Logger &setBoldBG() { return setColor(CurColor); };

private:
  ColorCode CurColor = LogColor::RESET;
  formatted_raw_ostream Os;
  bool IsBold; // bold font
  bool IsBg;   // background/foreground
};

enum class LogDataType {
  Str,
  OneInt,
  OneIntWithDesc,
  TwoInt,
};
struct LogData {
  const StringRef Key;
  const LogDataType Type;
  const union {
    StringRef Str;
    const uint64_t OneInt;
    const std::pair<uint32_t, StringRef> OneIntWithDesc;
    const std::pair<uint32_t, uint32_t> TwoInt;
  } Value;
};

class BufferedLogger : protected Logger {

public:
  BufferedLogger() : Logger() {}
  BufferedLogger(raw_fd_ostream &os) : Logger(os) {}

  void setFunction(const StringRef Func) { CurrFunc = Func; }

  void addLog(const StringRef Key, const StringRef Value) {
    LogData Data = {
        getLogStr(Key), LogDataType::Str, {.Str = getLogStr(Value)}};
    addLog(Data);
  }

  void addLog(const StringRef Key, const uint64_t Value) {
    LogData Data = {getLogStr(Key), LogDataType::OneInt, {.OneInt = Value}};
    addLog(Data);
  }

  /// The desc is only used to describe the value, hence we use the right value
  void addLog(const StringRef Key, const uint64_t Value, std::string &&Desc) {
    LogData Data = {getLogStr(Key),
                    LogDataType::OneIntWithDesc,
                    {.OneIntWithDesc = {Value, getLogStr(Desc)}}};
    addLog(Data);
  }

  void addLog(const StringRef Key, const uint32_t Value1,
              const uint32_t Value2) {
    LogData Data = {
        getLogStr(Key), LogDataType::TwoInt, {.TwoInt = {Value1, Value2}}};
    addLog(Data);
  }

  void addLog(const LogData &Data) {
    MaxKeyLength = std::max(MaxKeyLength, Data.Key.size());
    auto &Buffer = GroupedBuffer[CurrFunc];
    Buffer.push_back(Data);
  }

  void displayLogs(StringRef Title = "");

private:
  void printStrLog(const StringRef Value);
  void printOneIntLog(uint64_t Value);
  void printOneIntWithDescLog(const std::pair<uint32_t, StringRef> Value);
  void printTwoIntLog(std::pair<uint32_t, uint32_t> Value);
  void printKey(const StringRef Key);

  /// Use a inner storage for log string to avoid use-after-free issue.
  StringRef getLogStr(const StringRef str) { return LogStrStorage.save(str); }

private:
  static constexpr size_t MaxIntLength = 4;
  StringRef CurrFunc;
  BumpPtrAllocator Alloc;
  UniqueStringSaver LogStrStorage{Alloc};
  StringMap<SmallVector<LogData, 8>> GroupedBuffer;
  size_t MaxKeyLength = 0;
};

} // namespace __xsan

extern __xsan::BufferedLogger Log;