// Globals DXVK defines in its layer, not in the DXBC compiler.
//
// log.cpp references Logger::s_instance through every log call, and the compiler is built
// standalone here, so the instance lives in this translation unit. An empty file name keeps
// the logger from creating a log file inside the app sandbox.
#include "log/log.h"

namespace dxvk {

  Logger Logger::s_instance("");

}