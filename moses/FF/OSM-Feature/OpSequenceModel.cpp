#include <fstream>
#include "OpSequenceModel.h"
#include "osmHyp.h"
#include "util/check.hh"
#include "moses/Util.h"

using namespace std;
using namespace lm::ngram;

namespace Moses
{

OpSequenceModel::OpSequenceModel(const std::string &line)
  :StatefulFeatureFunction("OpSequenceModel", 5, line )
{

  ReadParameters();
}

void OpSequenceModel :: readLanguageModel(const char *lmFile)
{

  string unkOp = "_TRANS_SLF_";
  OSM = new Model(m_lmPath.c_str());
  State startState = OSM->NullContextState();
  State endState;
  unkOpProb = OSM->Score(startState,OSM->GetVocabulary().Index(unkOp),endState);
}


void OpSequenceModel::Load()
{

  readLanguageModel(m_lmPath.c_str());

}



void OpSequenceModel:: Evaluate(const Phrase &source
                                , const TargetPhrase &targetPhrase
                                , ScoreComponentCollection &scoreBreakdown
                                , ScoreComponentCollection &estimatedFutureScore) const
{

  osmHypothesis obj;
  obj.setState(OSM->NullContextState());
  WordsBitmap myBitmap(source.GetSize());
  vector <string> mySourcePhrase;
  vector <string> myTargetPhrase;
  vector<float> scores(5);
  vector <int> alignments;
  int startIndex = 0;
  int endIndex = source.GetSize();

  const AlignmentInfo &align = targetPhrase.GetAlignTerm();
  AlignmentInfo::const_iterator iter;


  for (iter = align.begin(); iter != align.end(); ++iter) {
    alignments.push_back(iter->first);
    alignments.push_back(iter->second);
  }

  for (int i = 0; i < targetPhrase.GetSize(); i++) {
    if (targetPhrase.GetWord(i).IsOOV())
      myTargetPhrase.push_back("_TRANS_SLF_");
    else
      myTargetPhrase.push_back(targetPhrase.GetWord(i).GetFactor(0)->GetString().as_string());
  }

  for (int i = 0; i < source.GetSize(); i++) {
    mySourcePhrase.push_back(source.GetWord(i).GetFactor(0)->GetString().as_string());
  }

  obj.setPhrases(mySourcePhrase , myTargetPhrase);
  obj.constructCepts(alignments,startIndex,endIndex-1,targetPhrase.GetSize());
  obj.computeOSMFeature(startIndex,myBitmap);
  obj.calculateOSMProb(*OSM);
  obj.populateScores(scores);
  estimatedFutureScore.PlusEquals(this, scores);

}


FFState* OpSequenceModel::Evaluate(
  const Hypothesis& cur_hypo,
  const FFState* prev_state,
  ScoreComponentCollection* accumulator) const
{
  const TargetPhrase &target = cur_hypo.GetCurrTargetPhrase();
  const WordsBitmap &bitmap = cur_hypo.GetWordsBitmap();
  WordsBitmap myBitmap = bitmap;
  const Manager &manager = cur_hypo.GetManager();
  const InputType &source = manager.GetSource();
  const Sentence &sourceSentence = static_cast<const Sentence&>(source);
  osmHypothesis obj;
  vector <string> mySourcePhrase;
  vector <string> myTargetPhrase;
  vector<float> scores(5);


  //target.GetWord(0)

  //cerr << target <<" --- "<<target.GetSourcePhrase()<< endl;  // English ...

  //cerr << align << endl;   // Alignments ...
  //cerr << cur_hypo.GetCurrSourceWordsRange() << endl;

  //cerr << source <<endl;

// int a = sourceRange.GetStartPos();
// cerr << source.GetWord(a);
  //cerr <<a<<endl;

  //const Sentence &sentence = static_cast<const Sentence&>(curr_hypo.GetManager().GetSource());


  const WordsRange & sourceRange = cur_hypo.GetCurrSourceWordsRange();
  int startIndex  = sourceRange.GetStartPos();
  int endIndex = sourceRange.GetEndPos();
  const AlignmentInfo &align = cur_hypo.GetCurrTargetPhrase().GetAlignTerm();
  osmState * statePtr;

  vector <int> alignments;



  AlignmentInfo::const_iterator iter;

  for (iter = align.begin(); iter != align.end(); ++iter) {
    //cerr << iter->first << "----" << iter->second << " ";
    alignments.push_back(iter->first);
    alignments.push_back(iter->second);
  }


  //cerr<<bitmap<<endl;
  //cerr<<startIndex<<" "<<endIndex<<endl;


  for (int i = startIndex; i <= endIndex; i++) {
    myBitmap.SetValue(i,0); // resetting coverage of this phrase ...
    mySourcePhrase.push_back(source.GetWord(i).GetFactor(0)->GetString().as_string());
    // cerr<<mySourcePhrase[i]<<endl;
  }

  for (int i = 0; i < target.GetSize(); i++) {

    if (target.GetWord(i).IsOOV())
      myTargetPhrase.push_back("_TRANS_SLF_");
    else
      myTargetPhrase.push_back(target.GetWord(i).GetFactor(0)->GetString().as_string());

  }


  //cerr<<myBitmap<<endl;

  obj.setState(prev_state);
  obj.constructCepts(alignments,startIndex,endIndex,target.GetSize());
  obj.setPhrases(mySourcePhrase , myTargetPhrase);
  obj.computeOSMFeature(startIndex,myBitmap);
  obj.calculateOSMProb(*OSM);
  obj.populateScores(scores);

  /*
    if (bitmap.GetFirstGapPos() == NOT_FOUND)
    {

      int xx;
  	 cerr<<bitmap<<endl;
  	 int a = bitmap.GetFirstGapPos();
  	 obj.print();
      cin>>xx;
    }
    */

  /*
    vector<float> scores(5);
    scores[0] = 0.343423f;
    scores[1] = 1.343423f;
    scores[2] = 2.343423f;
    scores[3] = 3.343423f;
    scores[4] = 4.343423f;
    */

  accumulator->PlusEquals(this, scores);

  return obj.saveState();




  //return statePtr;
// return NULL;
}

FFState* OpSequenceModel::EvaluateChart(
  const ChartHypothesis& /* cur_hypo */,
  int /* featureID - used to index the state in the previous hypotheses */,
  ScoreComponentCollection* accumulator) const
{
  abort();

}

const FFState* OpSequenceModel::EmptyHypothesisState(const InputType &input) const
{
  cerr << "OpSequenceModel::EmptyHypothesisState()" << endl;

  State startState = OSM->BeginSentenceState();

  return new osmState(startState);
}

std::string OpSequenceModel::GetScoreProducerWeightShortName(unsigned idx) const
{
  return "osm";
}

std::vector<float> OpSequenceModel::GetFutureScores(const Phrase &source, const Phrase &target) const
{
  ParallelPhrase pp(source, target);
  std::map<ParallelPhrase, Scores>::const_iterator iter;
  iter = m_futureCost.find(pp);
//iter = m_coll.find(pp);
  if (iter == m_futureCost.end()) {
    vector<float> scores(5, 0);
    scores[0] = unkOpProb;
    return scores;
  } else {
    const vector<float> &scores = iter->second;
    return scores;
  }
}

void OpSequenceModel::SetParameter(const std::string& key, const std::string& value)
{

  if (key == "path") {
    m_lmPath = value;
  } else if (key == "order") {
    lmOrder = Scan<int>(value);
  } else {
    StatefulFeatureFunction::SetParameter(key, value);
  }
}

} // namespace
