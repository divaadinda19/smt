// $Id$

#include "util/check.hh"
#include "FFState.h"
#include "StaticData.h"
#include "DWLScoreProducer.h"
#include "WordsRange.h"
#include "AlignmentInfo.h"
#include "Util.h"
#include <vector>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <stdexcept>
#include <exception>
#include <iterator>
#include "CeptTable.h"

using namespace std;
using namespace boost::bimaps;
using namespace PSD;

namespace Moses
{

DWLScoreProducer::DWLScoreProducer(ScoreIndexManager &scoreIndexManager, const vector<float> &weights) 
  : m_predictionCache(100)
{
  scoreIndexManager.AddScoreProducer(this);

  const_cast<StaticData&>(StaticData::Instance()).SetWeightsForScoreProducer(this, weights);
}

bool DWLScoreProducer::Initialize(const string &modelFile, const string &configFile, const string &ceptTableFile)
{
  m_extractorConfig.Load(configFile);
  m_ceptTable = new CeptTable(ceptTableFile);

  m_consumerFactory = new VWLibraryPredictConsumerFactory(modelFile, m_extractorConfig.GetVWOptionsPredict(), 255);
  m_extractor = new DWLFeatureExtractor(*m_ceptTable->GetTargetIndex(), m_extractorConfig, false);

  // set normalization function
  const string &normFunc = m_extractorConfig.GetNormalization();
  if (normFunc == "squared_loss") {
    m_normalizer = &NormalizeSquaredLoss;
  } else if (normFunc == "logistic_loss_basic") {
    m_normalizer = &NormalizeLogisticLossBasic;
  } else {
    throw runtime_error("Unknown normalization function: " + normFunc);
  }

  // set factors
  m_srcFactors = m_extractorConfig.GetDecoderSourceFactors();
  m_tgtFactors = m_extractorConfig.GetDecoderTargetFactors();

  return true;
};

ScoreComponentCollection DWLScoreProducer::ScoreFactory(float classifierPrediction, float oovCount, float nullCount)
{
  ScoreComponentCollection out;
  vector<float> scores(3);
  scores[0] = classifierPrediction;
  scores[1] = oovCount;
  scores[2] = nullCount;
  out.Assign(this, scores);
  return out;
}

const map<size_t, float> &DWLScoreProducer::GetCachedPredictions(
    const string &key, const string &srcCept, const InputType &src,
    const std::vector<std::pair<int, int> > &spanList)
{
  if (! m_predictionCache.HasKey(key)) {
    VERBOSE(2, "[DWL] Cache miss: " + key + "\n");
    if (m_ceptTable->SrcExists(srcCept)) {
      VERBOSE(2, "[DWL] Source found in cept table\n");
      const vector<CeptTranslation> &ceptTranslations = m_ceptTable->GetTranslations(srcCept);

      vector<CeptTranslation>::const_iterator transIt;
      IFVERBOSE(2) {
        VERBOSE(2, "[DWL] Possible translations of source '" + srcCept + "':\n");
        for (transIt = ceptTranslations.begin(); transIt != ceptTranslations.end(); transIt++) {
          VERBOSE(2, "    " + m_ceptTable->GetTgtString(transIt->m_index) + "\n");
        }
      }

      vector<float> ceptLosses(ceptTranslations.size());
      VWLibraryPredictConsumer *p_consumer = m_consumerFactory->Acquire();

      IFVERBOSE(2) {
        p_consumer->SetDebugOutfile("dlm" + SPrint<size_t>(++m_debugFileCounter) + ".debug");
      }
      m_extractor->GenerateFeatures(p_consumer, src.m_DWLContext, spanList, ceptTranslations, ceptLosses);
      m_consumerFactory->Release(p_consumer);

      m_normalizer(ceptLosses); // normalize using the function specified in config file
      
      map<size_t, float> toCache;
      vector<float>::const_iterator lossIt = ceptLosses.begin();
      for (transIt = ceptTranslations.begin(); transIt != ceptTranslations.end(); transIt++, lossIt++) {
        toCache[transIt->m_index] = *lossIt;
      }
      m_cacheLock.lock();
      m_predictionCache[key] = toCache;
      m_cacheLock.unlock();
    } else {
      m_cacheLock.lock();
      m_predictionCache[key] = map<size_t, float>();
      m_cacheLock.unlock();
      VERBOSE(2, "[DWL] Source NOT found in cept table\n");
    }
  }
  return m_predictionCache[key];
}

vector<ScoreComponentCollection> DWLScoreProducer::ScoreOptions(const vector<TranslationOption *> &options, const InputType &src)
{
  vector<ScoreComponentCollection> scores;

  if (options.size() != 0 && ! options[0]->IsOOV()) {
    VERBOSE(2, "[DWL] Scoring " + SPrint<size_t>(options.size()) + " translation options\n");
    vector<float> losses(options.size(), 0.0);
    vector<float> pruned(options.size(), 0.0);
    vector<float> nullAligned(options.size(), 0.0);

    // over all translation options for span
    for (size_t optIdx = 0; optIdx < options.size(); optIdx++) {
      const TranslationOption &option = *options[optIdx];
      // which source words are aligned to each target word
      map<size_t, vector<size_t> > alignedSrcWords;
      const AlignmentInfo &alignInfo = option.GetTargetPhrase().GetAlignmentInfo();
      AlignmentInfo::const_iterator aliIt;
      for (aliIt = alignInfo.begin(); aliIt != alignInfo.end(); aliIt++) {
        alignedSrcWords[aliIt->second].push_back(aliIt->first);
      }

      // over all words in translation option
      for (size_t tgtPos = 0; tgtPos < option.GetTargetPhrase().GetSize(); tgtPos++) {
        string tgtWord = option.GetTargetPhrase().GetWord(tgtPos).GetString(m_tgtFactors, false);
        VERBOSE(2, "[DWL] At target word: " + tgtWord + "\n");
        if (! alignedSrcWords[tgtPos].empty()) {

          // key for prediction cache
          string srcCept = GetSourceCept(src, options[optIdx]->GetStartPos(), alignedSrcWords[tgtPos]);
          VERBOSE(2, "[DWL] Source group: " + srcCept + "\n");
          string key = src.ToString() + SPrint(options[optIdx]->GetStartPos()) + "\n" + srcCept;
          
          const map<size_t, float> &predictions = GetCachedPredictions(
              key, srcCept, src, AlignToSpanList(options[optIdx]->GetStartPos(), alignedSrcWords[tgtPos]));
          bool knownTgt = false;
          size_t tgtIdx = m_ceptTable->GetTgtPhraseID(tgtWord, &knownTgt);
          if (knownTgt && predictions.find(tgtIdx) != predictions.end()) {
            float loss = predictions.find(tgtIdx)->second;
            losses[optIdx] += (Equals(loss, 0) ? LOWEST_SCORE : log(loss));
          } else {
            // source cept or target word was pruned
            VERBOSE(2, "[DWL] Target word NOT found\n");
            pruned[optIdx] += 1;
          }
        } else {
          VERBOSE(2, "[DWL] Scoring null-aligned target\n");
          nullAligned[optIdx] += 1;
        }
      } // words in translation options
    } // translation options

    // ok, losses now contains the product of translation probabilities (sum of their logs)
    // let's fill in the scores
    for (size_t i = 0; i < options.size(); i++) {
      scores.push_back(ScoreFactory(losses[i], pruned[i], nullAligned[i]));
    }
  } else if (options.size() != 0) {
    VERBOSE(2, "[DWL] Not scoring OOV: " + src.GetWord(options[0]->GetStartPos()).ToString() + "\n");
    for (size_t i = 0; i < options.size(); i++) {
      // Moses' OOVs are our OOVs too (and they are always 1-word phrases)
      scores.push_back(ScoreFactory(0, 1, 0));
    }
  }

  return scores;
}

size_t DWLScoreProducer::GetNumScoreComponents() const
{
  return 3; // VW prediction, target-side OOV count, null-aligned tgt word count
}

std::string DWLScoreProducer::GetScoreProducerDescription(unsigned) const
{
  return "DWL";
}

std::string DWLScoreProducer::GetScoreProducerWeightShortName(unsigned) const
{
  return "dwl";
}

size_t DWLScoreProducer::GetNumInputScores() const
{
  return 0;
}

void DWLScoreProducer::NormalizeSquaredLoss(vector<float> &losses)
{

	// This is (?) a good choice for sqrt loss (default loss function in VW)

  float sum = 0;

	// clip to [0,1] and take 1-Z as non-normalized prob
  vector<float>::iterator it;
  for (it = losses.begin(); it != losses.end(); it++) {
		if (*it <= 0.0) *it = 1.0;
		else if (*it >= 1.0) *it = 0.0;
		else *it = 1.0 - *it;
		sum += *it;
  }

  if (! Equals(sum, 0)) {
		// normalize
    for (it = losses.begin(); it != losses.end(); it++)
      *it /= sum;
  } else {
		// sum of non-normalized probs is 0, then take uniform probs
    for (it = losses.begin(); it != losses.end(); it++) 
      *it = 1.0 / losses.size();
  }
}

void DWLScoreProducer::NormalizeLogisticLossBasic(vector<float> &losses)
{

	// Use this with logistic loss (we switched to this in April/May 2013)

  float sum = 0;
  vector<float>::iterator it;
  for (it = losses.begin(); it != losses.end(); it++) {
    *it = exp(-*it);
    sum += *it;
  }
  for (it = losses.begin(); it != losses.end(); it++) {
    *it /= sum;
  }
}

void DWLScoreProducer::Normalize2(vector<float> &losses)
{
  float sum = 0;
  float minLoss;
  if (losses.size() > 0)
    minLoss = -losses[0];

  vector<float>::iterator it;
  for (it = losses.begin(); it != losses.end(); it++) {
    *it = -*it;
    minLoss = min(minLoss, *it);
  }

  for (it = losses.begin(); it != losses.end(); it++) {
    *it -= minLoss;
    sum += *it;
  }

  if (! Equals(sum, 0)) {
    for (it = losses.begin(); it != losses.end(); it++)
      *it /= sum;
  } else {
    for (it = losses.begin(); it != losses.end(); it++) 
      *it = 1.0 / losses.size();
  }
}

void DWLScoreProducer::Normalize3(vector<float> &losses)
{
  float sum = 0;
  vector<float>::iterator it;
  for (it = losses.begin(); it != losses.end(); it++) {
		// Alex changed this to be *it rather than -*it because it sorted backwards; not sure if this is right though!
    //*it = 1 / (1 + exp(-*it));
    *it = 1 / (1 + exp(*it));
    sum += *it;
  }
  for (it = losses.begin(); it != losses.end(); it++)
    *it /= sum;
}

string DWLScoreProducer::GetSourceCept(const InputType &src, size_t startPos, const vector<size_t> &positions)
{
  string out;
  out.reserve(positions.size() * 10); // guess the memory needed
  vector<size_t>::const_iterator it;
  for (it = positions.begin(); it != positions.end(); it++) {
    out += src.GetWord(startPos + *it).GetString(m_srcFactors, true);
  }
  if (out.size() > 0) out.resize(out.size() - 1); // get rid of last space
  return out;
}

vector<pair<int, int> > DWLScoreProducer::AlignToSpanList(size_t startPos, const std::vector<size_t> &positions)
{
  vector<pair<int, int> > out;

  int start = -1;
  int last = -1;
  vector<size_t>::const_iterator it;
  for (it = positions.begin(); it != positions.end(); it++) {
    if (start == -1) {
      last = start = *it;    
    } else if (*it == last + 1) {
      last = *it; // the span is still contiguous    
    } else {
      // discontinuity
      out.push_back(make_pair<int, int>(startPos + start, startPos + last + 1)); // C++-style interval: [begin, end)
      start = last = *it;    
    }
  }
  if (start != -1) {
    out.push_back(make_pair<int, int>(startPos + start, startPos + last + 1)); // final span
  } else {
    throw logic_error("Asked for span list for null-aligned word");
//    out.push_back(make_pair<int, int>(0, 0)); // null alignment
  }

  return out;
}

} // namespace Moses