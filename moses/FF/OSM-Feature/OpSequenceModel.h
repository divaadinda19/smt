#pragma once

#include <string>
#include <map>
#include <vector>
#include "moses/FF/StatefulFeatureFunction.h"
#include "moses/Manager.h"
#include "moses/FF/OSM-Feature/osmHyp.h"
#include "lm/model.hh"


namespace Moses
{

class OpSequenceModel : public StatefulFeatureFunction
{
public:


  lm::ngram::Model * OSM;

  int lmOrder;
  float unkOpProb;

  OpSequenceModel(const std::string &line);

  void readLanguageModel(const char *);
  void Load();

  FFState* Evaluate(
    const Hypothesis& cur_hypo,
    const FFState* prev_state,
    ScoreComponentCollection* accumulator) const;

  void  Evaluate(const Phrase &source
                 , const TargetPhrase &targetPhrase
                 , ScoreComponentCollection &scoreBreakdown
                 , ScoreComponentCollection &estimatedFutureScore) const;

  virtual FFState* EvaluateChart(
    const ChartHypothesis& /* cur_hypo */,
    int /* featureID - used to index the state in the previous hypotheses */,
    ScoreComponentCollection* accumulator) const;

  virtual const FFState* EmptyHypothesisState(const InputType &input) const;

  virtual std::string GetScoreProducerWeightShortName(unsigned idx=0) const;

  std::vector<float> GetFutureScores(const Phrase &source, const Phrase &target) const;
  void SetParameter(const std::string& key, const std::string& value);

  bool IsUseable(const FactorMask &mask) const {
    return true;
  }

protected:
  typedef std::pair<Phrase, Phrase> ParallelPhrase;
  typedef std::vector<float> Scores;
  std::map<ParallelPhrase, Scores> m_futureCost;

  std::vector < std::pair < std::set <int> , std::set <int> > > ceptsInPhrase;
  std::set <int> targetNullWords;
  std::string m_lmPath;


};


} // namespace
