/* Codex: журнал через ABI ядра, без файлового потока C++.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "logging.h"
#include "hmm_platform.h"
namespace Logging {
static int level=0;static bool support=false;
const char *GetDebugOptionName(int){return "HMM2";}
std::string GetTimeString(){return std::to_string(hmm_millis());}
void InitLog(){}
void setDebugLevel(int n){level=n;}int getDebugLevel(){return level;}
void setTextSupportMode(bool b){support=b;}bool isTextSupportModeEnabled(){return support;}
}
bool IS_DEBUG(int,int){return false;}
