#include "Vocabulary.hpp"
#include "Utils.hpp"
#include <fstream>
#include <iostream>
#include <pthread.h>

std::string Vocabulary::ThreadArg::file;
int Vocabulary::ThreadArg::numNegative;

Vocabulary::Vocabulary(const int wordVectorDim, const int contextLength, const int paragraphVectorDim):
  contextLen(contextLength),
  wordVecDim(wordVectorDim),
  paragraphVecDim(paragraphVectorDim),
  wordScoreVecDim(this->wordVecDim*this->contextLen+this->paragraphVecDim)
{}

void Vocabulary::read(const std::string& documentFile, COUNT freqThreshold){
  std::ifstream ifs(documentFile.c_str());
  std::unordered_map<std::string, INDEX> wordIndexTMP;
  std::vector<std::string> wordListTMP;
  std::vector<COUNT> wordCountTMP;
  std::vector<std::string> token;
  std::unordered_map<std::string, INDEX>::iterator it;
  COUNT unkCount = 0;
  INDEX paragraphIndex = 0;

  assert("Input file not found error!" && ifs);

  for (std::string line; std::getline(ifs, line);){
    ++paragraphIndex;
    Utils::split(line, token);

    for (int i = 0, size = token.size(); i < size; ++i){
      it = wordIndexTMP.find(token[i]);
	
      if (it != wordIndexTMP.end()){
	wordCountTMP[it->second] += 1;
      }
      else {
	wordIndexTMP[token[i]] = wordListTMP.size();
	wordListTMP.push_back(token[i]);
	wordCountTMP.push_back(1);
      }
    }
  }

  COUNT totalCount = 0;

  for (int i = 0, size = wordListTMP.size(); i < size; ++i){
    if (wordCountTMP[i] < freqThreshold){
      unkCount += wordCountTMP[i];
      continue;
    }

    this->wordIndex[wordListTMP[i]] = this->wordList.size();
    this->wordList.push_back(wordListTMP[i]);
    this->wordCount.push_back(wordCountTMP[i]);
    this->discardProb.push_back((double)this->wordCount.back());
    totalCount += this->wordCount.back();

    for (COUNT j = 0, numNoise = (COUNT)pow(this->wordCount.back(), 0.75); j < numNoise; ++j){
      this->noiseDistribution.push_back(this->wordIndex.at(this->wordList.back()));
    }
  }

  totalCount += unkCount;

  for (int i = 0, size = this->discardProb.size(); i < size; ++i){
    this->discardProb[i] /= totalCount;
    this->discardProb[i] = 1.0-sqrt(1.0e-05/this->discardProb[i]);
  }

  std::unordered_map<std::string, INDEX>().swap(wordIndexTMP);
  std::vector<std::string>().swap(wordListTMP);
  std::vector<COUNT>().swap(wordCountTMP);
  std::vector<std::string>().swap(token);
  
  this->unkIndex = this->wordList.size();
  this->nullIndex = this->unkIndex+1;
  this->wordList.push_back("**UNK**");
  this->wordList.push_back("**NULL**");
  this->wordVector = MatD::Random(this->wordVecDim, this->wordList.size())*sqrt(6.0/(this->wordVecDim*2+1.0));
  this->paragraphVector = MatD::Random(this->paragraphVecDim, paragraphIndex)*sqrt(6.0/(this->paragraphVecDim*2+1.0));
  this->wordScoreVector = MatD::Zero(this->wordScoreVecDim, this->wordList.size());

  std::cout << "Documents: " << paragraphIndex << std::endl;
  std::cout << "Vocabulary size: " << this->wordList.size() << std::endl;
  std::cout << "Word embedding size: " << this->wordVecDim << std::endl;
  std::cout << "Paragraph embedding size: " << this->paragraphVecDim << std::endl;
  std::cout << "Context size: " << this->contextLen << std::endl;
}

void Vocabulary::train(const std::string& documentFile, const double learningRate, const double shrink, const int numNegative, const int numThreads){
  static std::vector<Vocabulary::ThreadArg*> args;
  static int step = this->paragraphVector.cols()/numThreads;
  pthread_t pt[numThreads];

  if (args.empty()){
    Vocabulary::ThreadArg::file = documentFile;
    Vocabulary::ThreadArg::numNegative = numNegative;

    for (int i = 0; i < numThreads; ++i){
      args.push_back(new Vocabulary::ThreadArg(*this));
      args.back()->r = Rand(Rand::r_.next());
    }
  }

  for (int i = 0; i < numThreads; ++i){
    args[i]->beg = i*step;
    args[i]->end = (i == numThreads-1 ? this->paragraphVector.cols()-1 : (i+1)*step-1);
    args[i]->lr = learningRate;
    args[i]->shr = shrink/(args[i]->end-args[i]->beg+1);
    pthread_create(&pt[i], 0, Vocabulary::ThreadArg::threadFunc, (void*)args[i]);
  }

  for (int i = 0; i < numThreads; ++i){
    pthread_join(pt[i], 0);
  }
}

void* Vocabulary::ThreadArg::threadFunc(void* a){
  Vocabulary::ThreadArg* arg = (Vocabulary::ThreadArg*)a;
  std::ifstream ifs(Vocabulary::ThreadArg::file.c_str());
  std::string line;

  for (INDEX i = 0; i < arg->beg; ++i){
    std::getline(ifs, line);
  }

  INDEX paragraphIndex = arg->beg;
  std::vector<std::string> token;
  std::unordered_map<std::string, INDEX>::iterator it;
  std::vector<INDEX> paragraph;

  for (; std::getline(ifs, line) && paragraphIndex <= arg->end; ++paragraphIndex){
    paragraph.clear();
    Utils::split(line, token);

    for (int i = 0; i < arg->voc.contextLen; ++i){
      paragraph.push_back(arg->voc.nullIndex);
    }
    
    for (int i = 0, size = token.size(); i < size; ++i){
      it = arg->voc.wordIndex.find(token[i]);
      
      if (it == arg->voc.wordIndex.end()){
	paragraph.push_back(arg->voc.unkIndex);
      }
      else {
	paragraph.push_back(it->second);
      }
    }
    
    arg->voc.train(paragraphIndex, paragraph, arg->lr, Vocabulary::ThreadArg::numNegative, arg->r);
    arg->lr -= arg->shr;
  }

  std::vector<std::string>().swap(token);
  std::vector<INDEX>().swap(paragraph);
  pthread_exit(0);
}

void Vocabulary::train(const INDEX paragraphIndex, const std::vector<INDEX>& paragraph, const double learningRate, const int numNegative, Rand& rnd){
  MatD gradContext(this->wordVecDim, this->contextLen);
  MatD gradPara(this->paragraphVecDim, 1);
  std::unordered_map<INDEX, int> negHist;
  double deltaPos, deltaNeg;
  INDEX neg;

  for (int i = this->contextLen, size = paragraph.size(); i < size; ++i){
    if (paragraph[i] == this->unkIndex || this->discardProb[paragraph[i]] > rnd.zero2one()){
      continue;
    }
    
    gradContext.setZero();
    gradPara.setZero();
    negHist.clear();
    deltaPos = this->paragraphVector.col(paragraphIndex).dot(this->wordScoreVector.block(0, paragraph[i], this->paragraphVecDim, 1));

    for (int j = i-this->contextLen; j < i; ++j){
      deltaPos += this->wordVector.col(paragraph[j]).dot(this->wordScoreVector.block(this->paragraphVecDim+(j-i+this->contextLen)*this->wordVecDim, paragraph[i], this->wordVecDim, 1));
    }

    deltaPos = Utils::sigmoid(deltaPos)-1.0;
    gradPara = deltaPos*this->wordScoreVector.block(0, paragraph[i], this->paragraphVecDim, 1);

    for (int j = i-this->contextLen; j < i; ++j){
      gradContext.col(j-i+this->contextLen) = deltaPos*this->wordScoreVector.block(this->paragraphVecDim+(j-i+this->contextLen)*this->wordVecDim, paragraph[i], this->wordVecDim, 1);
    }

    deltaPos *= learningRate;
    this->wordScoreVector.block(0, paragraph[i], this->paragraphVecDim, 1) -= deltaPos*this->paragraphVector.col(paragraphIndex);

    for (int j = i-this->contextLen; j < i; ++j){
      this->wordScoreVector.block(this->paragraphVecDim+(j-i+this->contextLen)*this->wordVecDim, paragraph[i], this->wordVecDim, 1) -= deltaPos*this->wordVector.col(paragraph[j]);
    }

    for (int k = 0; k < numNegative; ++k){
      neg = paragraph[i];

      while (neg == paragraph[i] || negHist.count(neg)){
	neg = this->noiseDistribution[rnd.next()%this->noiseDistribution.size()];
      }

      negHist[neg] = 1;
      deltaNeg = this->paragraphVector.col(paragraphIndex).dot(this->wordScoreVector.block(0, neg, this->paragraphVecDim, 1));

      for (int j = i-this->contextLen; j < i; ++j){
	deltaNeg += this->wordVector.col(paragraph[j]).dot(this->wordScoreVector.block(this->paragraphVecDim+(j-i+this->contextLen)*this->wordVecDim, neg, this->wordVecDim, 1));
      }
      
      deltaNeg = Utils::sigmoid(deltaNeg);
      gradPara += deltaNeg*this->wordScoreVector.block(0, neg, this->paragraphVecDim, 1);
      
      for (int j = i-this->contextLen; j < i; ++j){
	gradContext.col(j-i+this->contextLen) += deltaNeg*this->wordScoreVector.block(this->paragraphVecDim+(j-i+this->contextLen)*this->wordVecDim, neg, this->wordVecDim, 1);
      }
      
      deltaNeg *= learningRate;
      this->wordScoreVector.block(0, neg, this->paragraphVecDim, 1) -= deltaNeg*this->paragraphVector.col(paragraphIndex);
      
      for (int j = i-this->contextLen; j < i; ++j){
	this->wordScoreVector.block(this->paragraphVecDim+(j-i+this->contextLen)*this->wordVecDim, neg, this->wordVecDim, 1) -= deltaNeg*this->wordVector.col(paragraph[j]);
      }
    }

    this->paragraphVector.col(paragraphIndex) -= learningRate*gradPara;

    for (int j = i-this->contextLen; j < i; ++j){
      this->wordVector.col(paragraph[j]) -= learningRate*gradContext.col(j-i+this->contextLen);
    }
  }

  std::unordered_map<INDEX, int>().swap(negHist);
}

void Vocabulary::outputParagraphVector(const std::string& fileName){
  std::ofstream ofs(fileName.c_str());

  for (int i = 0; i < this->paragraphVector.cols(); ++i){
    ofs << i;

    for (int j = 0; j < this->paragraphVector.rows(); ++j){
      ofs << " " << this->paragraphVector.coeff(j, i);
    }

    ofs << std::endl;
  }
}

void Vocabulary::outputWordVector(const std::string& fileName){
  std::ofstream ofs(fileName.c_str());

  for (int i = 0; i < this->wordVector.cols(); ++i){
    ofs << this->wordList[i];

    for (int j = 0; j < this->wordVector.rows(); ++j){
      ofs << " " << this->wordVector.coeff(j, i);
    }

    ofs << std::endl;
  }
}

void Vocabulary::save(const std::string& fileName){
  std::ofstream ofs(fileName.c_str(), std::ios::out|std::ios::binary);

  assert(ofs);
  Utils::save(ofs, this->wordVector);
  Utils::save(ofs, this->paragraphVector);
  Utils::save(ofs, this->wordScoreVector);
}

void Vocabulary::load(const std::string& fileName){
  std::ifstream ifs(fileName.c_str(), std::ios::in|std::ios::binary);

  assert(ifs);
  Utils::load(ifs, this->wordVector);
  Utils::load(ifs, this->paragraphVector);
  Utils::load(ifs, this->wordScoreVector);
}

void Vocabulary::wordKnn(const int k){
  printf("KNN words of words\n");

  for (std::string line; std::getline(std::cin, line) && line != "q"; ){
    if (!this->wordIndex.count(line)){
      continue;
    }

    INDEX target = this->wordIndex.at(line);
    MatD dist(1, this->wordList.size());

    for (INDEX i = 0; i < this->wordList.size(); ++i){
      dist.coeffRef(0, i) = (i == target ?
			     -1.0e+05:
			     Utils::cosDis(this->wordVector.col(target), this->wordVector.col(i)));
    }

    for (int i = 0; i < k; ++i){
      int row, col;

      dist.maxCoeff(&row, &col);
      dist.coeffRef(row, col) = -1.0e+05;
      printf("(%.5f) %s\n", Utils::cosDis(this->wordVector.col(col), this->wordVector.col(target)), this->wordList[col].c_str());
    }

    printf("\n");
  }
}
