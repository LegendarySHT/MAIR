#include "Logging.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FormattedStream.h"
#include <cstdint>
#include <string>

using namespace __xsan;
using namespace llvm;

__xsan::BufferedLogger Log;

Logger::ScopeLog::ScopeLog(Logger &Log, const char *Title)
    : Log(Log), Title(Title) {
  Log.unbold().white().delimiter('<', 20, Title);
}

Logger::ScopeLog::~ScopeLog() {
  Log.unbold().white().delimiter('>', 20);
  Log.log("\n");
  Log.reset();
  Log.flush();
}

Logger &Logger::delimiter(char c, uint8_t width, StringRef ID) {
  if (c == '-' && width == 10 && ID.empty()) {
    Os << "----------\n";
    return *this;
  }

  if (ID.empty()) {
    Os << std::string(width, c) << "\n";
    return *this;
  }

  if (ID.size() + 4 > width) {
    width = ID.size() + 4;
  }

  uint8_t SideWidth = (width - ID.size()) / 2;
  std::string Side = std::string(SideWidth, c);

  Os << Side << ' ' << ID << ' ' << Side << "\n";
  return *this;
}

void BufferedLogger::printKey(const StringRef Key) {
  unbold().white().log(Key).padToColumn(MaxKeyLength + 1).log(": ");
}

void BufferedLogger::printStrLog(const StringRef Value) {
  bold().magenta().log(Value).endl();
}
void BufferedLogger::printOneIntLog(uint64_t Value) {
  bold().green().log(Value).endl();
}
void BufferedLogger::printOneIntWithDescLog(
    const std::pair<uint32_t, StringRef> Value) {
  bold()
      .green()
      .log(Value.first)
      .padToColumn(MaxKeyLength + 3 + MaxIntLength)
      .white()
      .unbold()
      .log("(")
      .cyan()
      .log(Value.second)
      .white()
      .unbold()
      .log(")")
      .endl();
}

void BufferedLogger::printTwoIntLog(std::pair<uint32_t, uint32_t> Value) {
  bold()
      .red()
      .log(Value.first)
      .padToColumn(MaxKeyLength + 3 + MaxIntLength)
      .white()
      .unbold()
      .log(" --> ")
      .bold()
      .green()
      .log(Value.second)
      .endl();
}

void BufferedLogger::displayLogs() {
  if (GroupedBuffer.empty())
    return;
  auto ScopedLog = scope();
  for (StringRef Func : GroupedBuffer.keys()) {
    const auto &Buffer = GroupedBuffer[Func];
    if (Buffer.empty()) {
      continue;
    }
    printKey("Function Name");
    printStrLog(Func);
    for (auto Data : Buffer) {
      const StringRef Key = Data.Key;
      printKey(Key);
      switch (Data.Type) {
      case LogDataType::Str:
        printStrLog(Data.Value.Str);
        break;
      case LogDataType::OneInt:
        printOneIntLog(Data.Value.OneInt);
        break;
      case LogDataType::TwoInt:
        printTwoIntLog(Data.Value.TwoInt);
        break;
      }
    }
  }
}