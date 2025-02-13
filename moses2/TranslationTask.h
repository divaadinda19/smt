#pragma once
#include <string>
#include "legacy/ThreadPool.h"

namespace Moses2
{

class System;
class ManagerBase;
class Manager;

class TranslationTask: public Task
{
public:

  TranslationTask(System &system, const std::string &line, long translationId);
  virtual ~TranslationTask();
  virtual void Run();
  virtual std::string ReturnTranslation(bool nbest) const;

protected:
  ManagerBase *m_mgr;
};

}

