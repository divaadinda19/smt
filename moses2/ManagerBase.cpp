/*
 * Manager.cpp
 *
 *  Created on: 23 Oct 2015
 *      Author: hieu
 */
#include <boost/foreach.hpp>
#include <vector>
#include <sstream>
#include "System.h"
#include "ManagerBase.h"
#include "Phrase.h"
#include "InputPathsBase.h"
#include "InputPathBase.h"
#include "TranslationModel/PhraseTable.h"
#include "legacy/Range.h"
#include "PhraseBased/Sentence.h"

using namespace std;

namespace Moses2
{
ManagerBase::ManagerBase(System &sys, const TranslationTask &task,
                         const std::string &inputStr, long translationId)
  :system(sys)
  ,task(task)
  ,m_inputStr(inputStr)
  ,m_translationId(translationId)
  ,m_pool(NULL)
  ,m_systemPool(NULL)
  ,m_hypoRecycler(NULL)
  ,m_input(NULL)
{
}

ManagerBase::~ManagerBase()
{
  system.featureFunctions.CleanUpAfterSentenceProcessing(*m_input);

  GetPool().Reset();
  GetHypoRecycler().Clear();
}

void ManagerBase::InitPools()
{
  m_pool = &system.GetManagerPool();
  m_systemPool = &system.GetSystemPool();
  m_hypoRecycler = &system.GetHypoRecycler();
  //cerr << "pool size " << m_pool->Size() << " " << m_systemPool->Size() << endl;
}

}

