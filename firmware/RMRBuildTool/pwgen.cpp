#include "pwgen.h"
#include <QByteArray>
#include <QString>
#include <windows.h>
#include <bcrypt.h>

static void randBytes(quint8* out, int n)
{
    BCryptGenRandom(nullptr, out, n, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
}

QString generateStrongPassword()
{
    static const char upper[] = "ABCDEFGHJKLMNPQRSTUVWXYZ";
    static const char lower[] = "abcdefghijkmnopqrstuvwxyz";
    static const char digit[] = "23456789";
    static const char syms[]  = "!@#$%^&*-_=+?";
    const int len = 24;
    QByteArray pw(len, 0);
    int classes = 0;
    for (int i = 0; i < len; i++) {
        quint8 r[1];
        randBytes(r, 1);
        int pool = 0;
        switch (r[0] % 4) {
            case 0: pw[i] = upper[r[0] % (sizeof(upper) - 1)]; pool = 1; break;
            case 1: pw[i] = lower[r[0] % (sizeof(lower) - 1)]; pool = 2; break;
            case 2: pw[i] = digit[r[0] % (sizeof(digit) - 1)]; pool = 4; break;
            default: pw[i] = syms[r[0] % (sizeof(syms) - 1)];  pool = 8; break;
        }
        classes |= pool;
    }
    // Guarantee all four classes by replacing the first four positions.
    if (classes != 15) {
        quint8 r[4];
        randBytes(r, 4);
        pw[0] = upper[r[0] % (sizeof(upper) - 1)];
        pw[1] = lower[r[1] % (sizeof(lower) - 1)];
        pw[2] = digit[r[2] % (sizeof(digit) - 1)];
        pw[3] = syms[r[3] % (sizeof(syms) - 1)];
    }
    return QString::fromUtf8(pw);
}