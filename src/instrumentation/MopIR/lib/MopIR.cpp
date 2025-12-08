#include "../include/MopIR.h"
#include "../include/MopIRAnnotator.h"

using namespace __xsan::MopIR;

// 实现 annotateIR 方法（避免循环依赖）
void MopIR::annotateIR(AnnotationLevel Level, bool UseMetadata) {
  MopIRAnnotator Annotator(Level, UseMetadata);
  Annotator.annotate(*this);
}
