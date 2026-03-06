#ifndef ADSSYMBOLPARSER_H_
#define ADSSYMBOLPARSER_H_

#include <vector>
#include "AdsSymbolUploadInfo2.h"

class AdsSymbolParser
{
public:
  void retrieveSymbolsAndTypes();

private:
  std::vector<char> mSymbols;
  std::vector<char> mDatatypes;
};

#endif // ADSSYMBOLPARSER_H_