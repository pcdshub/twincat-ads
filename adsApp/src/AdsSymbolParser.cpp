#include "AdsSymbolParser.h"
#include "AdsDatatypeEntry.h"
#include "AdsDatatypeIndex.h"
#include "AdsSymbolIndex.h"
#include "AdsSymbolUploadInfo2.h"
#include <iostream>

AdsSymbolParser::AdsSymbolParser(long adsClientPort, const AmsAddr &amsAddr) : mAdsClientPort(adsClientPort), mAmsAddr(amsAddr), mRootNode(new SymbolNode) {}

long AdsSymbolParser::load(std::unordered_map<std::string, const AdsSymbolEntry *> &adsSymbolMap)
{
  mSymbols.clear();
  mDatatypes.clear();

  long errorCode = 0;
  auto symbolUploadInfo = AdsSymbolUploadInfo2::fromAmsAddr(mAdsClientPort, mAmsAddr, errorCode);
  if (errorCode)
  {
    printf("Failed to read symbol upload info. Error code: %lu\n", errorCode);
    return errorCode;
  }
  errorCode = symbolUploadInfo.uploadSymbols(mAdsClientPort, mAmsAddr, mSymbols);
  if (errorCode)
  {
    printf("Failed to read symbols. Error code: %lu\n", errorCode);
    return errorCode;
  }
  errorCode = symbolUploadInfo.uploadDatatypes(mAdsClientPort, mAmsAddr, mDatatypes);
  if (errorCode)
  {
    printf("Failed to read symbol datatypes. Error code: %lu\n", errorCode);
    return errorCode;
  }

  mSymbolIndex = std::make_shared<AdsSymbolIndex>(mSymbols);
  mDatatypeIndex = std::make_shared<AdsDatatypeIndex>(mDatatypes);

  buildSymbolTree();
  // std::cout << "----------" << std::endl;
  // printTree(mRootNode);
  std::cout << "Number of symbol nodes: " << treeSize << std::endl;
  std::cout << "Begin filling map..." << std::endl;
  adsSymbolMap.reserve(treeSize);
  fillAdsSymbolMap(mRootNode, adsSymbolMap);

  return errorCode;
}

AdsSymbolParser::~AdsSymbolParser()
{
  // Recursively delete all nodes
  std::vector<SymbolNode *> stack = {mRootNode};
  while (!stack.empty())
  {
    auto node = stack.back();
    stack.pop_back();
    stack.insert(stack.end(), node->children.begin(), node->children.end());
    delete node;
  }
}

void AdsSymbolParser::buildSymbolTree()
{
  std::cout << "Parsing symbol indices: " << std::endl;
  treeSize = 0;
  for (auto symbol : mSymbolIndex->entries())
  {
    std::string typeStr = symbol->type();
    std::string symbolName = symbol->name();
    // std::cout << "Try add symbol: name: [" << symbolName << "] type: [" << typeStr << "]";
    auto datatypeEntry = mDatatypeIndex->lookup(typeStr);
    if (!datatypeEntry)
    {
      // std::cout << " [NOTE: [" << typeStr << "] not in datatype index. No symbol added to the symbol tree for it]" << std::endl;
      continue;
    }
    // else
    //   std::cout << std::endl;
    addSymbol(mRootNode, symbol, symbolName, typeStr, datatypeEntry);
  }
}

void AdsSymbolParser::addSymbol(SymbolNode *parentNode, const AdsSymbolEntry *symbol, const std::string &symbolName, const std::string &symbolType, AdsDatatypeIndex::Entry *datatypeEntry)
{
  treeSize++;

  auto newNode = new SymbolNode();
  newNode->symbolName = symbolName;
  newNode->symbolType = symbolType;
  newNode->parent = parentNode;
  newNode->children = std::list<SymbolNode *>();
  newNode->symbol = symbol;
  newNode->datatypeEntry = datatypeEntry;
  parentNode->children.push_back(newNode);

  if (!datatypeEntry)
    return;

  for (auto child : datatypeEntry->children(*mDatatypeIndex))
  {
    std::string newSymbolName = symbolName + "." + child->name();
    std::string symbolType = child->adsType()->type();
    auto newSymbol = new AdsSymbolEntry();
    newSymbol->entryLength = child->adsType()->entryLength;
    newSymbol->iGroup = newNode->group();
    newSymbol->iOffs = newNode->offset() + child->offset();
    newSymbol->size = child->adsType()->size;
    newSymbol->dataType = child->adsType()->dataType;
    newSymbol->flags = child->adsType()->flags;
    newSymbol->nameLength = child->adsType()->nameLength;
    newSymbol->typeLength = child->adsType()->typeLength;
    newSymbol->commentLength = child->adsType()->commentLength;
    // std::cout << "Try add symbol: name: [" << newSymbolName << "] type: [" << symbolType << "]" << std::endl;
    addSymbol(newNode, newSymbol, newSymbolName, symbolType, child);
  }
}

void AdsSymbolParser::printTree(SymbolNode *node)
{
  if (!node)
    return;

  if (node->parent == mRootNode || node->children.empty())
  {
    std::cout << "name:  [" << node->symbolName << "] type: [" << node->symbolType << "]" << std::endl;
    if (node->symbol)
      std::cout << "group: [" << node->symbol->iGroup << "] offset: [" << node->symbol->iOffs << "]" << std::endl;
    else
      std::cout << "no symbol found" << std::endl;
    std::cout << "----------" << std::endl;
  }
  for (auto child : node->children)
  {
    printTree(child);
  }
}

void AdsSymbolParser::fillAdsSymbolMap(SymbolNode *node, std::unordered_map<std::string, const AdsSymbolEntry *> &adsSymbolMap)
{
  if (!node)
    return;

  if (node->symbol)
  {
    if (adsSymbolMap.size() % 1000 == 0)
      std::cout << "Map size at: " << adsSymbolMap.size() << std::endl;
    adsSymbolMap[node->symbolName] = node->symbol;
  }

  for (auto child : node->children)
  {
    fillAdsSymbolMap(child, adsSymbolMap);
  }
}