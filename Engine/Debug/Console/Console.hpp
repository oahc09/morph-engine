#pragma once
#include <array>
#include <deque>
#include "Engine/Core/common.hpp"
#include "Engine/Core/Delegate.hpp"
#include "Engine/Core/Rgba.hpp"
#include "Engine/Core/Utils.hpp"
#include "Engine/Math/Primitives/IntRange.hpp"
#include "Engine/Core/StringUtils.hpp"
#include <mutex>

class Command;
class Camera;
class Font;

using CommandHandler = delegate<bool(Command&)> ;


class Console {
  struct CommandDef {
    std::string mName = "";
    std::string mParamInfo;
    std::string mDescription;
    CommandHandler mHandle;

    template<typename Functor>
    CommandDef(const std::string& name, const std::string& paramInfo, const std::string& description, Functor&& cb)
      : mName(name)
      , mParamInfo(paramInfo)
      , mDescription(description)
      , mHandle(std::forward<Functor>(cb)) {}
    CommandDef() = default;
  };


public:
  enum Severity {
    DEBUG_INFO,
    DEBUG_WARN,
    DEBUG_ERROR,
  };
  static bool isOpen() { return Get()->mIsOpened; };
  static Console* Get();
  static void log(const std::string& msg, Severity level);
  static void log(const std::string& msg, const Rgba& color);
  static void info(const std::string& msg);
  static void warn(const std::string& msg);
  static void error(const std::string& msg);
  static constexpr uint MAX_COMMAND_COUNT = 1000u;
  static constexpr float FONT_SIZE = 20;
  static constexpr float FONT_ASPECT = 0.7f;
  static constexpr float WORD_PADDING = 1;
  static constexpr float SCROLLBAR_WIDTH = 3;
  static constexpr uint MAX_LOG_NUMBER = 100u;
  float LINE_HEIGHT = 0.f;
public:
  void init();;
  template<typename T>
  void input(const T& val) {
    std::string str = toString(val);
    input(v);
  }

  template<>
  void input(const std::string& val) {
    if (mSelection.size() > 0) {
      erase(mSelection.min, mSelection.max);
      mSelection.reset();
    }
    mInputStream.insert(mInputStream.cbegin() + mCursorPosition, val.cbegin(), val.cend());
    mCursorPosition+=(uint)val.length();
  }

  template<>
  void input(const char& c) {
    if (mSelection.size() > 0) {
      erase(mSelection.min, mSelection.max);
      mSelection.reset();
    }
    mInputStream.insert(mInputStream.cbegin() + mCursorPosition, c);
    mCursorPosition++;
  }

  class OutputHandler {
  public:
    OutputHandler(std::function<void(std::string, Severity)>* func, Console* owner)
      : func(func), owner(owner) {}
    OutputHandler(OutputHandler&& o): func(o.func), owner(o.owner) {
      o.func = nullptr;
    }

    OutputHandler& operator=(OutputHandler&& o) {
      owner = o.owner;
      func = o.func;
      o.owner = nullptr;
      o.func = nullptr;
    };

    bool release() {
      if (func == nullptr) return false;
      for(size_t i = owner->mOutputHandler.size() - 1; i < owner->mOutputHandler.size(); --i) {
        auto* handle = &owner->mOutputHandler[i];
        if( handle == func) {
          std::swap(owner->mOutputHandler.back(), owner->mOutputHandler[i]);
          owner->mOutputHandler.pop_back();
          func = nullptr;
          return true;
        }
      }
      return false;
    }
  protected:
    std::function<void(std::string, Severity)>* func;
    Console* owner;
  };
  
  void replaceInput(std::string str);
  void update(float deltaSecond);
  void render() const;

  void open();
  void close();
  void toggle();

  inline bool opened() const { return mIsOpened; }
  bool exec(const std::string& cmd);
  bool exec();
  template<typename Functor>
  bool hook(const std::string& cmdName, const std::string& paramInfo, const std::string& description, Functor&& cb);
  bool clear();

  template<typename T>
  OutputHandler onOutput(T&& func) {
    auto* handler = &mOutputHandler.emplace_back(func);
    return { handler, this };
  }
protected:
  Console();
  ~Console();
  CommandDef* findCommand(const std::string& name);
  void autoCompleteNext(const std::string& txt);
  void consoleCharHandler(unsigned msg, size_t wParam, size_t lParam);
  void consoleControlHandler(unsigned msg, size_t wParam, size_t lParam);
  void print(const CommandDef& cmdDef);

  void record(const std::string& msg, Severity level);
  void record(const std::string& msg, const Rgba& color);

  void erase(uint from, uint to);

  bool mIsOpened = false;

  // input state control
  std::string mAutoCompleteText;
  std::string mInputStream;
  IntRange mSelection;
  Rgba mFontColor;
  uint mCursorPosition = 0;
  bool mIsSelecting = false ;
  bool mDrawCursor = true;
  float mCursorFlashSec = 0.f;
  uint mLineNumberAtBottom = 0;
  uint mTotalLineLogCanRender = 0;
  uint mAutoCompleteIndex = 0;

  // display window content
  std::deque<std::string> mLogStream;

  std::array<owner<CommandDef*>, MAX_COMMAND_COUNT> mlegalCommands;
  std::vector<std::function<void(std::string, Severity)>> mOutputHandler;
  std::vector<std::string> mCommandLog;
  uint mNumCurrentCommand = 0;
  uint mNextCommandLogIndex = 0;
  Camera* mCamera = nullptr;
  mutable std::mutex mLogStreamLock;
  S<const Font> mFont;
private:
  void hookInBuiltInCommand();
};

template<typename Functor>
bool Console::hook(const std::string& cmdName, const std::string& paramInfo, const std::string& description,
  Functor&& cb) {
  for(uint i = 0; i<mNumCurrentCommand; i++) {
    if (mlegalCommands[i]->mName == cmdName) return false;
  }

  mlegalCommands[mNumCurrentCommand++] = new CommandDef(cmdName, paramInfo, description, std::forward<Functor>(cb));

  return true;
}


#define COMMAND_REG(name, param, desc) \
static struct APPEND(__command, __LINE__) { \
  APPEND(__command, __LINE__)(const std::string& cmdName, const std::string& paramInfo, const std::string& description) { \
    Console::Get()->hook(cmdName, paramInfo, description, exec); \
  }; \
  static bool exec(Command&); \
} APPEND(__commandIns, __LINE__)(name, param, desc); \
inline bool APPEND(__command, __LINE__)::exec