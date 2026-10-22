// SPDX-License-Identifier: GPL-3.0-only
// D3DXAssembleShader for vs_1_1, vs_2_0, ps_1_1 to ps_1_4 and ps_2_0 text, written against the D3D9 token format and
// checked token for token against retail d3dx9 (DirectX 9.0 SDK) on the game's shaders and a probe corpus
// (port/d3dx/tests: asm_corpus.py, asm_cases.py, asm_compare.py). What retail accepts, rejects and how it encodes
// was measured, not assumed: see port/d3dx/README.md. Newlines are whitespace to the grammar, as in retail; a
// syntax error ends the parse, while register, mask and number errors are reported and parsing goes on.
#include <d3dx9.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

namespace {

enum Version { VS11 = 1, VS20 = 2, PS11 = 4, PS12 = 8, PS13 = 16, PS14 = 32, PS20 = 64 };
const unsigned VS = VS11 | VS20, PS1 = PS11 | PS12 | PS13 | PS14, PS = PS1 | PS20, ALL = VS | PS;

enum RegType { R_TEMP = 0, R_INPUT = 1, R_CONST = 2, R_ADDR = 3, R_TEXTURE = 3, R_RASTOUT = 4, R_ATTROUT = 5,
               R_TEXCRDOUT = 6, R_COLOROUT = 8, R_DEPTHOUT = 9, R_SAMPLER = 10 };

// Operand shapes: D = destination only, D1..D3 = destination and sources.
enum Shape { NONE, D, D1, D2, D3 };

struct Op
{
    const char *name;
    unsigned opcode;
    Shape shape;
    unsigned versions;
};

const Op kOps[] = {
    {"nop", 0x00, NONE, ALL},       {"mov", 0x01, D1, ALL},          {"add", 0x02, D2, ALL},
    {"sub", 0x03, D2, ALL},         {"mad", 0x04, D3, ALL},          {"mul", 0x05, D2, ALL},
    {"rcp", 0x06, D1, VS | PS20},   {"rsq", 0x07, D1, VS | PS20},    {"dp3", 0x08, D2, ALL},
    {"dp4", 0x09, D2, VS | PS12 | PS13 | PS14 | PS20},              {"min", 0x0a, D2, VS | PS20},
    {"max", 0x0b, D2, VS | PS20},   {"slt", 0x0c, D2, VS},           {"sge", 0x0d, D2, VS},
    {"exp", 0x0e, D1, VS | PS20},   {"log", 0x0f, D1, VS | PS20},    {"lit", 0x10, D1, VS},
    {"dst", 0x11, D2, VS},          {"lrp", 0x12, D3, PS | VS20},    {"frc", 0x13, D1, VS | PS20},
    {"m4x4", 0x14, D2, VS | PS20},  {"m4x3", 0x15, D2, VS | PS20},   {"m3x4", 0x16, D2, VS | PS20},
    {"m3x3", 0x17, D2, VS | PS20},  {"m3x2", 0x18, D2, VS | PS20},   {"pow", 0x20, D2, VS20 | PS20},
    {"crs", 0x21, D2, VS20 | PS20}, {"sgn", 0x22, D3, VS20},         {"abs", 0x23, D1, VS20 | PS20},
    {"nrm", 0x24, D1, VS20 | PS20}, {"sincos", 0x25, D3, VS20 | PS20}, {"mova", 0x2e, D1, VS20},
    {"texcoord", 0x40, D, PS11 | PS12 | PS13},                      {"texcrd", 0x40, D1, PS14},
    {"texkill", 0x41, D, PS},       {"tex", 0x42, D, PS11 | PS12 | PS13},
    {"texld", 0x42, D1, PS14},      {"texbem", 0x43, D1, PS11 | PS12 | PS13},
    {"texbeml", 0x44, D1, PS11 | PS12 | PS13},                      {"texreg2ar", 0x45, D1, PS11 | PS12 | PS13},
    {"texreg2gb", 0x46, D1, PS11 | PS12 | PS13},                    {"texm3x2pad", 0x47, D1, PS11 | PS12 | PS13},
    {"texm3x2tex", 0x48, D1, PS11 | PS12 | PS13},                   {"texm3x3pad", 0x49, D1, PS11 | PS12 | PS13},
    {"texm3x3tex", 0x4a, D1, PS11 | PS12 | PS13},                   {"texm3x3spec", 0x4c, D2, PS11 | PS12 | PS13},
    {"texm3x3vspec", 0x4d, D1, PS11 | PS12 | PS13},                 {"expp", 0x4e, D1, VS},
    {"logp", 0x4f, D1, VS},         {"cnd", 0x50, D3, PS1},          {"texreg2rgb", 0x52, D1, PS12 | PS13},
    {"texdp3tex", 0x53, D1, PS12 | PS13},                           {"texm3x2depth", 0x54, D1, PS13},
    {"texdp3", 0x55, D1, PS12 | PS13}, {"texm3x3", 0x56, D1, PS12 | PS13}, {"texdepth", 0x57, D, PS14},
    {"cmp", 0x58, D3, PS12 | PS13 | PS14 | PS20},                   {"bem", 0x59, D2, PS14},
    {"dp2add", 0x5a, D3, PS20},     {"phase", 0xfffd, NONE, PS14},
};

const char *const kUsages[] = {"position", "blendweight", "blendindices", "normal", "psize", "texcoord", "tangent",
                               "binormal", "tessfactor", "positiont", "color", "fog", "depth", "sample"};

enum TokKind { T_END, T_IDENT, T_INT, T_FLOAT, T_PUNCT };

struct Token
{
    TokKind kind;
    std::string text;
    int line;
    double fval;
    unsigned long long ival;
};

struct Fatal
{
};

struct Reg
{
    unsigned type;
    unsigned num;
    bool relative;
    bool scalar; // oFog, oPts: no write mask
    bool valid;
};

class Assembler
{
public:
    Assembler(const char *text, size_t size) : mP(text), mEnd(text + size), mLine(1) {}

    bool Run()
    {
        try
        {
            ParseVersion();
            Advance();
            while (mTok.kind != T_END)
                Statement();
            mOut.push_back(0x0000ffff);
        }
        catch (const Fatal &)
        {
            return false;
        }
        return !mFailed;
    }

    std::vector<DWORD> mOut;
    std::string mMessages;

private:
    const char *mP, *mEnd;
    int mLine;
    Token mTok;
    unsigned mVersion = 0;
    bool mFailed = false;

    // ---- messages
    void Message(int line, bool error, int code, const char *fmt, ...)
    {
        char text[512];
        va_list args;
        va_start(args, fmt);
        vsnprintf(text, sizeof(text), fmt, args);
        va_end(args);
        char head[64];
        snprintf(head, sizeof(head), "(%d): %s X%d: ", line, error ? "error" : "warning", code);
        mMessages += head;
        mMessages += text;
        mMessages += "\n";
        if (error)
            mFailed = true;
    }
    [[noreturn]] void Unexpected(const Token &t)
    {
        if (t.kind == T_END)
            Message(t.line, true, 2000, "syntax error : unexpected end of file");
        else if (t.kind == T_FLOAT)
            Message(t.line, true, 2000, "syntax error : unexpected float '%g'", t.fval);
        else if (t.kind == T_INT)
            Message(t.line, true, 2000, "syntax error : unexpected integer '%s'", t.text.c_str());
        else
            Message(t.line, true, 2000, "syntax error : unexpected token '%s'", t.text.c_str());
        throw Fatal();
    }

    // ---- lexer
    void Skip()
    {
        while (mP < mEnd)
        {
            char c = *mP;
            if (c == '\n')
            {
                mLine++;
                mP++;
            }
            else if (c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f')
                mP++;
            else if (c == ';' || (c == '/' && mP + 1 < mEnd && mP[1] == '/'))
            {
                while (mP < mEnd && *mP != '\n')
                    mP++;
            }
            else if (c == '/' && mP + 1 < mEnd && mP[1] == '*')
            {
                mP += 2;
                while (mP < mEnd && !(*mP == '*' && mP + 1 < mEnd && mP[1] == '/'))
                {
                    if (*mP == '\n')
                        mLine++;
                    mP++;
                }
                mP = mP < mEnd ? mP + 2 : mEnd;
            }
            else
                break;
        }
    }
    static bool IsAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
    static bool IsDigit(char c) { return c >= '0' && c <= '9'; }

    void Advance()
    {
        Skip();
        mTok = Token();
        mTok.line = mLine;
        if (mP >= mEnd)
        {
            mTok.kind = T_END;
            return;
        }
        const char *s = mP;
        if (IsAlpha(*mP))
        {
            while (mP < mEnd && (IsAlpha(*mP) || IsDigit(*mP)))
                mP++;
            mTok.kind = T_IDENT;
        }
        else if (IsDigit(*mP) || (*mP == '.' && mP + 1 < mEnd && IsDigit(mP[1])))
        {
            if (*mP == '0' && mP + 1 < mEnd && (mP[1] == 'x' || mP[1] == 'X'))
            {
                mP += 2;
                unsigned long long v = 0;
                while (mP < mEnd && isxdigit((unsigned char)*mP))
                {
                    char c = *mP++;
                    v = v * 16 + (IsDigit(c) ? c - '0' : (c | 0x20) - 'a' + 10);
                    if (v > 0xffffffffULL)
                        v = 0x100000000ULL;
                }
                mTok.kind = T_INT;
                mTok.ival = v;
            }
            else
            {
                while (mP < mEnd && IsDigit(*mP))
                    mP++;
                if (mP < mEnd && *mP == '.')
                {
                    mP++;
                    while (mP < mEnd && IsDigit(*mP))
                        mP++;
                    // An exponent takes a '-' but not a '+', as retail's lexer does; then an optional f.
                    const char *e = mP;
                    if (e < mEnd && (*e == 'e' || *e == 'E'))
                    {
                        e++;
                        if (e < mEnd && *e == '-')
                            e++;
                        if (e < mEnd && IsDigit(*e))
                        {
                            while (e < mEnd && IsDigit(*e))
                                e++;
                            mP = e;
                        }
                    }
                    mTok.kind = T_FLOAT;
                    mTok.fval = strtod(std::string(s, mP).c_str(), NULL);
                    if (mP < mEnd && (*mP == 'f' || *mP == 'F'))
                        mP++;
                }
                else
                {
                    mTok.kind = T_INT;
                    unsigned long long v = 0;
                    for (const char *q = s; q < mP; q++)
                        v = v > 0xffffffffULL ? v : v * 10 + (*q - '0');
                    mTok.ival = v;
                }
            }
        }
        else
        {
            mP++;
            mTok.kind = T_PUNCT;
        }
        mTok.text.assign(s, mP);
    }
    bool IsPunct(char c) const { return mTok.kind == T_PUNCT && mTok.text[0] == c; }
    void Expect(char c)
    {
        if (!IsPunct(c))
            Unexpected(mTok);
        Advance();
    }

    // ---- version
    void ParseVersion()
    {
        Skip();
        const char *s = mP;
        while (mP < mEnd && (IsAlpha(*mP) || IsDigit(*mP) || *mP == '.'))
            mP++;
        std::string v(s, mP);
        int line = mLine;
        if (v.size() == 6 && (v[2] == '.' || v[2] == '_') && v[4] == v[2] && IsDigit(v[3]) && IsDigit(v[5]))
        {
            std::string kind = v.substr(0, 2);
            int major = v[3] - '0', minor = v[5] - '0';
            if (kind == "vs" && major == 1 && minor <= 1)
            {
                if (minor == 0)
                    Message(line, false, 2015, "vs_1_0 is no longer supported; using vs_1_1");
                mVersion = VS11;
                mOut.push_back(0xfffe0101);
                return;
            }
            if (kind == "vs" && major == 2 && minor == 0)
            {
                mVersion = VS20;
                mOut.push_back(0xfffe0200);
                return;
            }
            if (kind == "ps" && major == 1 && minor <= 4)
            {
                if (minor == 0)
                    Message(line, false, 2015, "ps_1_0 is no longer supported; using ps_1_1");
                static const unsigned kPs[] = {PS11, PS11, PS12, PS13, PS14};
                mVersion = kPs[minor];
                mOut.push_back(0xffff0100 | (minor == 0 ? 1 : minor));
                return;
            }
            if (kind == "ps" && major == 2 && minor == 0)
            {
                mVersion = PS20;
                mOut.push_back(0xffff0200);
                return;
            }
        }
        Message(line, true, 2001, "shader version expected");
        throw Fatal();
    }
    bool Is(unsigned versions) const { return (mVersion & versions) != 0; }
    bool Major2() const { return Is(VS20 | PS20); }

    // ---- registers
    static DWORD TypeBits(unsigned type) { return ((type & 7) << 28) | (((type >> 3) & 3) << 11); }

    bool Lookup(const std::string &name, Reg &r) const
    {
        r = Reg();
        r.valid = true;
        struct Named { const char *name; unsigned type, num, versions; bool scalar; };
        static const Named kNamed[] = {{"oPos", R_RASTOUT, 0, VS, false}, {"oFog", R_RASTOUT, 1, VS, true},
                                       {"oPts", R_RASTOUT, 2, VS, true}, {"oDepth", R_DEPTHOUT, 0, PS20, false},
                                       {"a0", R_ADDR, 0, VS, false}};
        for (const Named &n : kNamed)
            if (name == n.name)
            {
                r.type = n.type;
                r.num = n.num;
                r.scalar = n.scalar;
                return Is(n.versions);
            }
        size_t i = 0;
        while (i < name.size() && !IsDigit(name[i]))
            i++;
        if (i == 0 || i == name.size() || name.size() - i > 9)
            return false;
        for (size_t k = i; k < name.size(); k++)
            if (!IsDigit(name[k]))
                return false;
        if (name[i] == '0' && name.size() - i > 1)
            return false;
        std::string prefix = name.substr(0, i);
        unsigned long n = strtoul(name.c_str() + i, NULL, 10);
        r.num = (unsigned)n;
        struct Bank { const char *prefix; unsigned type; unsigned versions; unsigned long limit; };
        static const Bank kBanks[] = {
            {"r", R_TEMP, VS | PS20, 12},     {"r", R_TEMP, PS11 | PS12 | PS13, 2}, {"r", R_TEMP, PS14, 6},
            {"v", R_INPUT, VS, 16},           {"v", R_INPUT, PS, 2},                {"c", R_CONST, VS, 0x100000},
            {"c", R_CONST, PS1, 8},           {"c", R_CONST, PS20, 32},             {"t", R_TEXTURE, PS11 | PS12 | PS13, 4},
            {"t", R_TEXTURE, PS14, 6},        {"t", R_TEXTURE, PS20, 8},            {"s", R_SAMPLER, PS20, 16},
            {"oC", R_COLOROUT, PS20, 4},      {"oD", R_ATTROUT, VS, 2},             {"oT", R_TEXCRDOUT, VS, 8},
        };
        for (const Bank &b : kBanks)
            if (prefix == b.prefix && Is(b.versions))
            {
                r.type = b.type;
                return n < b.limit;
            }
        return false;
    }

    // Parses a register at the current identifier (the text without any _modifier suffix is name).
    Reg Register(const std::string &name, int line)
    {
        Reg r;
        if (name == "c" && IsPunct('['))
        {
            std::string text = "c[";
            Advance();
            r = Reg();
            r.type = R_CONST;
            r.valid = true;
            unsigned offset = 0;
            bool rel = false;
            auto indexReg = [&]() {
                std::string reg = mTok.text;
                int l = mTok.line;
                Advance();
                bool okx = false;
                if (IsPunct('.'))
                {
                    Advance();
                    okx = mTok.kind == T_IDENT && mTok.text == "x";
                    reg += "." + mTok.text;
                    Advance();
                }
                if (reg.compare(0, 2, "a0") != 0 || !okx || !Is(VS))
                    Message(l, true, 2007, "only a0.x is allowed as a relative address register in vs_1_1");
                text += reg;
                rel = true;
            };
            if (mTok.kind == T_INT)
            {
                offset = (unsigned)mTok.ival;
                text += mTok.text;
                Advance();
                if (IsPunct('+'))
                {
                    text += "+";
                    Advance();
                    if (mTok.kind != T_IDENT)
                        Unexpected(mTok);
                    indexReg();
                }
            }
            else if (mTok.kind == T_IDENT)
            {
                indexReg();
                if (IsPunct('+'))
                {
                    text += "+";
                    Advance();
                    if (mTok.kind != T_INT)
                        Unexpected(mTok);
                    offset = (unsigned)mTok.ival;
                    text += mTok.text;
                    Advance();
                }
            }
            else
                Unexpected(mTok);
            if (!IsPunct(']'))
                Unexpected(mTok);
            text += "]";
            Advance();
            r.num = offset;
            r.relative = rel;
            if (!Is(VS))
            {
                Message(line, true, 2005, "invalid register '%s'", text.c_str());
                r.valid = false;
            }
            return r;
        }
        if (!Lookup(name, r))
        {
            Message(line, true, 2005, "invalid register '%s'", name.c_str());
            r.valid = false;
        }
        if (r.type == R_CONST && IsPunct('['))
        {
            // cN[a0.x]
            Advance();
            std::string reg = mTok.text;
            int l = mTok.line;
            Advance();
            bool okx = false;
            if (IsPunct('.'))
            {
                Advance();
                okx = mTok.kind == T_IDENT && mTok.text == "x";
                Advance();
            }
            if (reg != "a0" || !okx || !Is(VS))
                Message(l, true, 2007, "only a0.x is allowed as a relative address register in vs_1_1");
            Expect(']');
            r.relative = true;
        }
        return r;
    }

    static int Component(char c)
    {
        switch (c)
        {
        case 'x': case 'r': return 0;
        case 'y': case 'g': return 1;
        case 'z': case 'b': return 2;
        case 'w': case 'a': return 3;
        }
        return -1;
    }

    // A '.' suffix: the identifier after it, or empty.
    std::string Suffix()
    {
        if (!IsPunct('.'))
            return std::string();
        Advance();
        if (mTok.kind != T_IDENT)
            Unexpected(mTok);
        std::string s = mTok.text;
        Advance();
        return s;
    }

    DWORD Dest(unsigned *maskOut = NULL, bool allowScalarMask = false)
    {
        if (mTok.kind != T_IDENT)
            Unexpected(mTok);
        std::string name = mTok.text;
        int line = mTok.line;
        Advance();
        Reg r = Register(name, line);
        std::string m = Suffix();
        unsigned mask = 0xf;
        if (!m.empty())
        {
            if (r.scalar && !allowScalarMask)
                Message(line, true, 2022, "scalar registers cannot be masked");
            mask = 0;
            int last = -1;
            bool ok = m.size() <= 4;
            for (char c : m)
            {
                int k = Component(c);
                if (k <= last)
                    ok = false;
                else
                {
                    mask |= 1u << k;
                    last = k;
                }
            }
            if (!ok)
                Message(line, true, 2003, "invalid mask '%s'", m.c_str());
        }
        if (maskOut)
            *maskOut = mask;
        return 0x80000000 | TypeBits(r.type) | (mask << 16) | (r.num & 0x7ff) | (r.relative ? 0x2000 : 0);
    }

    DWORD Source(bool scalarDefault)
    {
        int line = mTok.line;
        unsigned mod = 0;
        bool neg = false, comp = false;
        if (IsPunct('-'))
        {
            neg = true;
            Advance();
        }
        else if (Is(PS1) && mTok.kind == T_INT && mTok.ival == 1 && mTok.text == "1")
        {
            Advance();
            Expect('-');
            comp = true;
        }
        if (mTok.kind != T_IDENT)
            Unexpected(mTok);
        std::string name = mTok.text;
        line = mTok.line;
        Advance();
        // ps_1_x source modifiers are spelt as a suffix of the register name.
        std::string suffix;
        if (Is(PS1))
        {
            size_t u = name.find('_');
            if (u != std::string::npos)
            {
                static const char *const kMods[] = {"bias", "bx2", "x2", "dz", "db", "dw", "da"};
                std::string s = name.substr(u + 1);
                bool known = false;
                for (const char *k : kMods)
                    known |= s == k;
                if (known)
                {
                    suffix = s;
                    name = name.substr(0, u);
                }
            }
        }
        Reg r = Register(name, line);
        if (suffix == "bias")
            mod = neg ? 3 : 2;
        else if (suffix == "bx2")
            mod = neg ? 5 : 4;
        else if (suffix == "x2")
            mod = neg ? 8 : 7;
        else if (suffix == "dz" || suffix == "db")
            mod = 9;
        else if (suffix == "dw" || suffix == "da")
            mod = 10;
        else if (comp)
            mod = 6;
        else if (neg)
            mod = 1;
        std::string s = Suffix();
        unsigned swz = scalarDefault ? 0xff : 0xe4;
        if (!s.empty())
        {
            int c[4];
            int n = 0;
            for (char ch : s)
            {
                int k = Component(ch);
                if (k < 0 || n == 4)
                {
                    Message(line, true, 2004, "invalid swizzle '%s'", s.c_str());
                    k = 0;
                }
                if (n < 4)
                    c[n++] = k;
            }
            while (n < 4)
            {
                c[n] = c[n - 1];
                n++;
            }
            swz = c[0] | c[1] << 2 | c[2] << 4 | c[3] << 6;
            // A written .xyzw counts as no swizzle: a scalar instruction still reads .w.
            if (scalarDefault && swz == 0xe4)
                swz = 0xff;
        }
        return 0x80000000 | TypeBits(r.type) | (swz << 16) | (mod << 24) | (r.num & 0x7ff) | (r.relative ? 0x2000 : 0);
    }

    void Comma() { Expect(','); }

    void Emit(unsigned opcode, std::vector<DWORD> params, bool coissue, unsigned control = 0)
    {
        DWORD inst = opcode | (control << 16) | (coissue ? 0x40000000 : 0);
        if (Major2())
            inst |= (DWORD)params.size() << 24;
        mOut.push_back(inst);
        mOut.insert(mOut.end(), params.begin(), params.end());
    }

    float Number()
    {
        bool neg = false;
        if (IsPunct('-') || IsPunct('+'))
        {
            neg = IsPunct('-');
            Advance();
        }
        float v;
        if (mTok.kind == T_INT)
        {
            if (mTok.ival > 0xffffffffULL)
                Message(mTok.line, true, 1004, "decimal value truncated to 32bits");
            v = (float)(unsigned)mTok.ival;
        }
        else if (mTok.kind == T_FLOAT)
            v = (float)mTok.fval;
        else
            Unexpected(mTok);
        Advance();
        return neg ? -v : v;
    }

    void Declaration(const std::string &mnemonic, int line, bool coissue)
    {
        std::string rest = mnemonic.size() > 3 ? mnemonic.substr(4) : std::string();
        bool pp = false;
        DWORD usage = 0x80000000;
        bool sampler = false;
        if (Is(PS20))
        {
            if (rest == "pp")
                pp = true;
            else if (rest == "2d" || rest == "cube" || rest == "volume")
            {
                sampler = true;
                usage |= (rest == "2d" ? 2u : rest == "cube" ? 3u : 4u) << 27;
            }
            else if (!rest.empty())
            {
                Message(line, true, 2000, "syntax error : unexpected token '%s'", mnemonic.c_str());
                Message(line, true, 2024, "invalid instruction modifiers '%s'", mnemonic.c_str());
                throw Fatal();
            }
        }
        else
        {
            size_t d = rest.size();
            while (d > 0 && IsDigit(rest[d - 1]))
                d--;
            std::string name = rest.substr(0, d);
            unsigned index = d < rest.size() ? (unsigned)atoi(rest.c_str() + d) : 0;
            int code = -1;
            for (int k = 0; k < (int)(sizeof(kUsages) / sizeof(kUsages[0])); k++)
                if (name == kUsages[k])
                    code = k;
            if (code < 0)
            {
                Message(line, true, 2000, "syntax error : unexpected token '%s'", mnemonic.c_str());
                throw Fatal();
            }
            usage |= (DWORD)code | (index << 16);
        }
        (void)sampler;
        DWORD dest = Dest();
        if (pp)
            dest |= 2u << 20;
        Emit(0x1f, {usage, dest}, coissue);
    }

    void Statement()
    {
        bool coissue = false;
        int coissueLine = 0;
        if (IsPunct('+'))
        {
            coissue = true;
            coissueLine = mTok.line;
            Advance();
        }
        Instruction(coissue);
        // Retail reports a coissue it cannot take once the instruction has parsed.
        if (coissue && !Is(PS1))
            Message(coissueLine, true, 2027, "instruction coissue is not supported in this shader version");
    }

    void Instruction(bool coissue)
    {
        if (mTok.kind != T_IDENT)
            Unexpected(mTok);
        std::string mnemonic = mTok.text;
        int line = mTok.line;
        Advance();

        if (mnemonic.compare(0, 3, "dcl") == 0 && (mnemonic.size() == 3 || mnemonic[3] == '_') && Is(VS | PS20))
        {
            Declaration(mnemonic, line, coissue);
            return;
        }
        if (mnemonic == "def")
        {
            DWORD dest = Dest();
            std::vector<DWORD> p{dest};
            for (int k = 0; k < 4; k++)
            {
                Comma();
                float f = Number();
                DWORD bits;
                memcpy(&bits, &f, 4);
                p.push_back(bits);
            }
            Emit(0x51, p, coissue);
            return;
        }

        size_t u = mnemonic.find('_');
        std::string base = mnemonic.substr(0, u);
        unsigned control = 0;
        if (Is(PS20) && (base == "texldp" || base == "texldb"))
        {
            control = base == "texldp" ? 1 : 2;
            base = "texld";
        }
        const Op *op = NULL;
        for (const Op &o : kOps)
            if (base == o.name)
            {
                op = &o;
                break;
            }
        if (op && base == "texld" && Is(PS20))
        {
            static const Op kTexld20 = {"texld", 0x42, D2, PS20};
            op = &kTexld20;
        }
        if (!op)
        {
            Message(line, true, 2000, "syntax error : unexpected token '%s'", mnemonic.c_str());
            throw Fatal();
        }
        if (!Is(op->versions))
        {
            Message(line, true, 2000, "syntax error : unexpected token '%s'", mnemonic.c_str());
            Message(line, true, 2023, "'%s' is not a valid instruction in this shader version", mnemonic.c_str());
            throw Fatal();
        }
        unsigned resmod = 0, shift = 0;
        bool badmod = false;
        for (size_t s = u; s != std::string::npos;)
        {
            size_t e = mnemonic.find('_', s + 1);
            std::string m = mnemonic.substr(s + 1, e == std::string::npos ? std::string::npos : e - s - 1);
            if (m.empty() && e == std::string::npos)
                ; // a trailing '_' is accepted
            else if (m == "sat" && !Is(VS))
                resmod |= 1;
            else if (m == "pp" && Is(PS20))
                resmod |= 2;
            else if (Is(PS1) && (m == "x2" || m == "x4" || m == "d2"))
                shift = m == "x2" ? 1 : m == "x4" ? 2 : 0xf;
            else if (Is(PS14) && (m == "x8" || m == "d4" || m == "d8"))
                shift = m == "x8" ? 3 : m == "d4" ? 0xe : 0xd;
            else
                badmod = true;
            s = e;
        }
        if (badmod)
        {
            Message(line, true, 2000, "syntax error : unexpected token '%s'", mnemonic.c_str());
            Message(line, true, 2024, "invalid instruction modifiers '%s'", mnemonic.c_str());
            throw Fatal();
        }

        if (op->opcode == 0xfffd)
        {
            mOut.push_back(0x0000fffd);
            return;
        }
        std::vector<DWORD> p;
        if (op->shape != NONE)
        {
            DWORD dest = Dest() | (resmod << 20) | (shift << 24);
            // In vs a full mask on a matrix destination, written or not, becomes the rows the matrix has: .xy for
            // m3x2, .xyz for m3x3 and m4x3; a partial mask stays as written (ps_2_0 keeps every mask).
            if (Is(VS) && (op->opcode == 0x18 || op->opcode == 0x17 || op->opcode == 0x15) &&
                ((dest >> 16) & 0xf) == 0xf)
                dest = (dest & ~0x000f0000u) | ((op->opcode == 0x18 ? 0x3u : 0x7u) << 16);
            p.push_back(dest);
        }
        int sources = op->shape == D1 ? 1 : op->shape == D2 ? 2 : op->shape == D3 ? 3 : 0;
        // vs scalar instructions read .w when the source has no swizzle.
        bool scalar = Is(VS) && (op->opcode == 0x06 || op->opcode == 0x07 || op->opcode == 0x0e ||
                                 op->opcode == 0x0f || op->opcode == 0x4e || op->opcode == 0x4f);
        for (int k = 0; k < sources; k++)
        {
            Comma();
            p.push_back(Source(scalar));
        }
        unsigned opcode = op->opcode;
        // vs and ps_2_0 have no sub token: retail writes add with the second source negated.
        if (opcode == 0x03 && Is(VS | PS20))
        {
            opcode = 0x02;
            DWORD m = (p[2] >> 24) & 0xf;
            p[2] = (p[2] & ~0x0f000000u) | ((DWORD)(m == 1 ? 0 : m == 0 ? 1 : m) << 24);
        }
        Emit(opcode, p, coissue, control);
    }
};

} // namespace

extern "C" HRESULT WINAPI D3DXAssembleShader(LPCSTR pSrcData, UINT SrcDataLen, CONST D3DXMACRO *pDefines,
                                             LPD3DXINCLUDE pInclude, DWORD Flags, LPD3DXBUFFER *ppShader,
                                             LPD3DXBUFFER *ppErrorMsgs)
{
    (void)pDefines;
    (void)pInclude;
    (void)Flags;
    if (ppShader)
        *ppShader = NULL;
    if (ppErrorMsgs)
        *ppErrorMsgs = NULL;
    if (!pSrcData)
        return D3DERR_INVALIDCALL;
    Assembler a(pSrcData, SrcDataLen);
    bool ok = a.Run();
    if (!a.mMessages.empty() && ppErrorMsgs)
    {
        LPD3DXBUFFER msg;
        if (SUCCEEDED(D3DXCreateBuffer((DWORD)a.mMessages.size() + 1, &msg)))
        {
            memcpy(msg->GetBufferPointer(), a.mMessages.c_str(), a.mMessages.size() + 1);
            *ppErrorMsgs = msg;
        }
    }
    if (!ok)
        return D3DXERR_INVALIDDATA;
    if (ppShader)
    {
        LPD3DXBUFFER code;
        HRESULT hr = D3DXCreateBuffer((DWORD)(a.mOut.size() * 4), &code);
        if (FAILED(hr))
            return hr;
        memcpy(code->GetBufferPointer(), a.mOut.data(), a.mOut.size() * 4);
        *ppShader = code;
    }
    return S_OK;
}
