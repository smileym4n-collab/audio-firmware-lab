#pragma once

class RuntimeMode {
 public:
  virtual ~RuntimeMode() = default;

  virtual bool begin() = 0;
  virtual void loop() = 0;
  virtual const char *name() const = 0;
  virtual void prepareForRestart() {}
};
