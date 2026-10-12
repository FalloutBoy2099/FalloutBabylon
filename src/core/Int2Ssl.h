#pragma once

#include <QByteArray>
#include <QString>

// Runs the external int2ssl decompiler (compiled Fallout script .int -> readable SSL).
namespace Int2Ssl {

// Looks next to the application, in a tools/ folder beside it, and in $BABYLON_INT2SSL.
// Empty when the tool cannot be found.
QString findExecutable();

// Returns the SSL text, or an empty string with `error` filled. Fallout 1 scripts use another opcode set:
// when the default run fails, the Fallout 1 mode (-1) is tried too.
QString decompile(const QByteArray &intData, QString *error = nullptr, bool *usedFallout1Mode = nullptr);

}
