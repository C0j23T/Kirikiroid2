// Adapted from krkrsdl3 plugins/scriptsEx.cpp; see LICENSE-THIRD-PARTY.md.
// Scripts.* extensions the supported KAG scripts expect: member enumeration
// and counting, structure comparison, clone/foreach, property access with
// explicit flags, storage evaluation and fuzzy search. Attached to the core
// Scripts object by the module scriptsEx.dll.
#include "ncbind/ncbind.hpp"

#include <cassert>
#include <limits>
#include <vector>

#include "md5.h"
#include "PluginImpl.h"
#include "ScriptMgnIntf.h"
#include "StorageIntf.h"
#include "TextStream.h"
#include "tjsArray.h"
#include "tjsDictionary.h"
#include "tjsNative.h"
#include "tjsObject.h"

#define NCB_MODULE_NAME TJS_W("scriptsEx.dll")

namespace {

bool IsArray(iTJSDispatch2 *object) {
    iTJSNativeInstance *instance = nullptr;
    return TJS_SUCCEEDED(object->NativeInstanceSupport(
        TJS_NIS_GETINSTANCE, TJSGetArrayClassID(), &instance));
}

iTJSDispatch2 *ObjectOf(const tTJSVariant &value) {
    return value.Type() == tvtObject ? value.AsObjectNoAddRef() : nullptr;
}

iTJSDispatch2 *ObjectOf(const tTJSVariant *value) {
    return value ? ObjectOf(*value) : nullptr;
}

bool IsFunction(const tTJSVariantClosure &closure) {
    return closure.IsInstanceOf(0, nullptr, nullptr, TJS_W("Function"), nullptr) == TJS_S_TRUE;
}

bool IsDictionary(const tTJSVariantClosure &closure) {
    return closure.IsInstanceOf(0, nullptr, nullptr, TJS_W("Dictionary"), nullptr) == TJS_S_TRUE;
}

void SortArray(iTJSDispatch2 *array) {
    static tjs_uint hint = 0;
    array->FuncCall(0, TJS_W("sort"), &hint, nullptr, 0, nullptr, array);
}

class KeyCollector : public tTJSNativeFunction {
    std::vector<ttstr> &Names;

public:
    explicit KeyCollector(std::vector<ttstr> &names)
        : tTJSNativeFunction(TJS_W("Scripts.getObjectKeys")), Names(names) {}

    tjs_error TJS_INTF_METHOD Process(tTJSVariant *result, tjs_int numparams,
        tTJSVariant **param, iTJSDispatch2 *objthis) override {
        if (result) *result = (tjs_int)1;
        if (numparams >= 1) Names.push_back(ttstr(*param[0]));
        return TJS_S_OK;
    }
};

tTJSVariant GetObjectKeys(const tTJSVariant &value) {
    iTJSDispatch2 *array = TJSCreateArrayObject();
    tTJSVariant keys(array, array);
    array->Release();

    iTJSDispatch2 *object = ObjectOf(value);
    if (!object) return keys;

    if (IsArray(object)) {
        tjs_int count = TJSGetArrayElementCount(object);
        for (tjs_int i = 0; i < count; ++i) {
            tTJSVariant index(i);
            array->PropSetByNum(TJS_MEMBERENSURE, i, &index, array);
        }
    } else {
        std::vector<ttstr> names;
        KeyCollector collector(names);
        tTJSVariantClosure callback(&collector, nullptr);
        object->EnumMembers(TJS_IGNOREPROP | TJS_ENUM_NO_VALUE, &callback, object);
        for (tjs_int i = 0; i < (tjs_int)names.size(); ++i) {
            tTJSVariant name(names[i]);
            array->PropSetByNum(TJS_MEMBERENSURE, i, &name, array);
        }
        SortArray(array);
    }
    return keys;
}

tjs_int GetObjectCount(const tTJSVariant &value) {
    iTJSDispatch2 *object = ObjectOf(value);
    if (!object) return 0;
    if (IsArray(object)) return TJSGetArrayElementCount(object);
    tjs_int count = 0;
    if (TJS_FAILED(object->GetCount(&count, nullptr, nullptr, nullptr))) return 0;
    return count;
}

bool EqualStruct(const tTJSVariant &left, const tTJSVariant &right, bool numeric_loose);

bool IsNumber(const tTJSVariant &value) {
    return value.Type() == tvtInteger || value.Type() == tvtReal;
}

class MemberComparer : public tTJSNativeFunction {
    tTJSVariantClosure Target;
    bool Loose;

public:
    bool Match = true;

    MemberComparer(const tTJSVariantClosure &target, bool loose)
        : tTJSNativeFunction(TJS_W("Scripts.equalStruct")), Target(target), Loose(loose) {}

    tjs_error TJS_INTF_METHOD Process(tTJSVariant *result, tjs_int numparams,
        tTJSVariant **param, iTJSDispatch2 *objthis) override {
        if (result) *result = (tjs_int)Match;
        if (numparams < 3 || !Match) return TJS_S_OK;
        if ((tjs_int)*param[1] & TJS_HIDDENMEMBER) return TJS_S_OK;

        tTJSVariant target_value;
        tjs_error hr = Target.PropGet(
            TJS_MEMBERMUSTEXIST, param[0]->GetString(), nullptr, &target_value, nullptr);
        Match = TJS_SUCCEEDED(hr) && EqualStruct(*param[2], target_value, Loose);
        if (result) *result = (tjs_int)Match;
        return TJS_S_OK;
    }
};

bool EqualArray(const tTJSVariant &left, const tTJSVariant &right, bool loose) {
    iTJSDispatch2 *left_array = left.AsObjectNoAddRef();
    iTJSDispatch2 *right_array = right.AsObjectNoAddRef();
    tjs_int count = TJSGetArrayElementCount(left_array);
    if (count != TJSGetArrayElementCount(right_array)) return false;

    tTJSVariantClosure &left_closure = left.AsObjectClosureNoAddRef();
    tTJSVariantClosure &right_closure = right.AsObjectClosureNoAddRef();
    for (tjs_int i = 0; i < count; ++i) {
        tTJSVariant left_value, right_value;
        if (TJS_FAILED(left_closure.PropGetByNum(TJS_IGNOREPROP, i, &left_value, nullptr)))
            return false;
        if (TJS_FAILED(right_closure.PropGetByNum(TJS_IGNOREPROP, i, &right_value, nullptr)))
            return false;
        if (!EqualStruct(left_value, right_value, loose)) return false;
    }
    return true;
}

bool EqualDictionary(const tTJSVariant &left, const tTJSVariant &right, bool loose) {
    if (!EqualStruct(GetObjectKeys(left), GetObjectKeys(right), loose)) return false;

    MemberComparer comparer(right.AsObjectClosureNoAddRef(), loose);
    tTJSVariantClosure callback(&comparer, nullptr);
    left.AsObjectClosureNoAddRef().EnumMembers(TJS_IGNOREPROP, &callback, nullptr);
    return comparer.Match;
}

bool EqualStruct(const tTJSVariant &left, const tTJSVariant &right, bool numeric_loose) {
    if (left.Type() == tvtObject && right.Type() == tvtObject) {
        if (left.AsObjectNoAddRef() == right.AsObjectNoAddRef()) return true;

        tTJSVariantClosure &left_closure = left.AsObjectClosureNoAddRef();
        tTJSVariantClosure &right_closure = right.AsObjectClosureNoAddRef();
        if (IsFunction(left_closure) && IsFunction(right_closure))
            return left.DiscernCompare(right);

        bool left_array = IsArray(left.AsObjectNoAddRef());
        bool right_array = IsArray(right.AsObjectNoAddRef());
        if (left_array && right_array) return EqualArray(left, right, numeric_loose);
        if (IsDictionary(left_closure) && IsDictionary(right_closure))
            return EqualDictionary(left, right, numeric_loose);
    } else if (numeric_loose && IsNumber(left) && IsNumber(right)) {
        return left.NormalCompare(right);
    }
    return left.DiscernCompare(right);
}

class GuestFunction {
    tTJSVariantClosure Closure;
    std::vector<tTJSVariant *> Arguments;

public:
    tTJSVariant Result;

    GuestFunction(const tTJSVariantClosure &function, iTJSDispatch2 *this_object,
        tjs_int count, tTJSVariant **params) : Arguments(params, params + count) {
        Closure.Object = function.Object;
        Closure.ObjThis = function.ObjThis ? function.ObjThis : this_object;
        Closure.AddRef();
    }
    ~GuestFunction() { Closure.Release(); }
    GuestFunction(const GuestFunction &) = delete;
    GuestFunction &operator=(const GuestFunction &) = delete;

    bool IsValid() const { return Closure.Object != nullptr; }

    bool Call(tTJSVariant &key, tTJSVariant &value) {
        Arguments[0] = &key;
        Arguments[1] = &value;
        Result.Clear();
        tjs_error hr = Closure.FuncCall(0, nullptr, nullptr, &Result,
            static_cast<tjs_int>(Arguments.size()), Arguments.data(), Closure.ObjThis);
        if (TJS_FAILED(hr)) TJSThrowFrom_tjs_error(hr, TJS_W("Scripts.foreach"));
        return Result.Type() == tvtVoid;
    }
};

class EnumForwarder : public tTJSNativeFunction {
    GuestFunction &Guest;

public:
    explicit EnumForwarder(GuestFunction &guest)
        : tTJSNativeFunction(TJS_W("Scripts.foreach")), Guest(guest) {}

    tjs_error TJS_INTF_METHOD Process(tTJSVariant *result, tjs_int numparams,
        tTJSVariant **param, iTJSDispatch2 *objthis) override {
        bool next = true;
        if (numparams >= 3 && !((tjs_int)*param[1] & TJS_HIDDENMEMBER))
            next = Guest.Call(*param[0], *param[2]);
        if (result) *result = (tjs_int)next;
        return TJS_S_OK;
    }
};

tTJSVariant Clone(const tTJSVariant &value);

class CloneMember : public tTJSNativeFunction {
    iTJSDispatch2 *Target;

public:
    explicit CloneMember(iTJSDispatch2 *target)
        : tTJSNativeFunction(TJS_W("Scripts.clone")), Target(target) {}

    tjs_error TJS_INTF_METHOD Process(tTJSVariant *result, tjs_int numparams,
        tTJSVariant **param, iTJSDispatch2 *objthis) override {
        if (result) *result = (tjs_int)1;
        if (numparams < 3) return TJS_E_BADPARAMCOUNT;
        tTJSVariant value = Clone(*param[2]);
        tjs_error hr = Target->PropSet(TJS_MEMBERENSURE | static_cast<tjs_int>(*param[1]),
            param[0]->GetString(), nullptr, &value, Target);
        return hr;
    }
};

tTJSVariant Clone(const tTJSVariant &value) {
    iTJSDispatch2 *object = ObjectOf(value);
    if (!object) return value;
    tTJSVariantClosure &closure = value.AsObjectClosureNoAddRef();

    if (IsArray(object)) {
        iTJSDispatch2 *array = TJSCreateArrayObject();
        tTJSVariant result(array, array);
        array->Release();
        static tjs_uint add_hint = 0;
        tjs_int count = TJSGetArrayElementCount(object);
        for (tjs_int i = 0; i < count; ++i) {
            tTJSVariant member;
            tjs_error hr = closure.PropGetByNum(TJS_IGNOREPROP, i, &member, nullptr);
            if (TJS_FAILED(hr)) TJSThrowFrom_tjs_error(hr, TJS_W("Scripts.clone"));
            member = Clone(member);
            tTJSVariant *args[] = { &member };
            hr = array->FuncCall(0, TJS_W("add"), &add_hint, nullptr, 1, args, array);
            if (TJS_FAILED(hr)) TJSThrowFrom_tjs_error(hr, TJS_W("Scripts.clone"));
        }
        return result;
    }

    if (IsDictionary(closure)) {
        iTJSDispatch2 *dictionary = TJSCreateDictionaryObject();
        tTJSVariant result(dictionary, dictionary);
        dictionary->Release();
        CloneMember member(dictionary);
        tTJSVariantClosure callback(&member, nullptr);
        tjs_error hr = closure.EnumMembers(TJS_IGNOREPROP, &callback, nullptr);
        if (TJS_FAILED(hr)) TJSThrowFrom_tjs_error(hr, TJS_W("Scripts.clone"));
        return result;
    }

    tTJSVariant result;
    static tjs_uint clone_hint = 0;
    if (closure.FuncCall(0, TJS_W("clone"), &clone_hint, &result, 0, nullptr, nullptr)
        == TJS_S_TRUE)
        return result;
    return value;
}

struct TryCall {
    tjs_error Result = TJS_S_OK;
    virtual ~TryCall() = default;
    virtual void Run() = 0;
};

void RunTryCall(void *data) {
    static_cast<TryCall *>(data)->Run();
}

bool RethrowException(void *, const tTVPExceptionDesc &description) {
    throw description;
}

template <typename F>
tjs_error CallSafely(F &&function) {
    struct Call : TryCall {
        F Function;
        explicit Call(F function) : Function(static_cast<F &&>(function)) {}
        void Run() override { Result = Function(); }
    } call(static_cast<F &&>(function));
    TVPDoTryBlock(RunTryCall, RethrowException, nullptr, &call);
    return call.Result;
}

template <typename CHAR, typename MASK>
tjs_int BitapFuzzySearch(const CHAR *text, size_t text_length, const CHAR *pattern,
    size_t pattern_length, size_t max_errors, size_t char_bits) {
    static_assert(std::numeric_limits<MASK>::is_integer
            && !std::numeric_limits<MASK>::is_signed
            && std::numeric_limits<MASK>::radix == 2,
        "MASK must be an unsigned integer type");

    if (!text || !text_length || !pattern) return -3;
    if (!pattern_length) return 0;
    if (pattern_length >= sizeof(MASK) * std::numeric_limits<MASK>::digits) return -2;

    const size_t char_mask = (size_t)((1uLL << char_bits) - 1);
    const MASK end_of_pattern = (MASK)(1uLL << pattern_length);
    std::vector<MASK> rows(max_errors + 1, (MASK)~1uLL);
    std::vector<MASK> pattern_mask(char_mask + 1, (MASK)~0uLL);
    for (size_t i = 0; i < pattern_length; ++i)
        pattern_mask[char_mask & pattern[i]] &= (MASK)~(1uLL << i);

    for (size_t i = 0; i < text_length; ++i) {
        MASK previous = rows[0];
        const MASK text_mask = pattern_mask[char_mask & text[i]];
        rows[0] |= text_mask;
        rows[0] <<= 1;
        for (size_t distance = 1; distance <= max_errors; ++distance) {
            const MASK current = rows[distance];
            rows[distance] = (previous & (rows[distance] | text_mask)) << 1;
            previous = current;
        }
        if (0 == (rows[max_errors] & end_of_pattern)) {
            tjs_int result = (tjs_int)(1 + i - pattern_length);
            assert(result >= 0);
            return result;
        }
    }
    return -1;
}

class ScriptsExt {
public:
    static tTJSVariant clone(tTJSVariant value) { return Clone(value); }

    static tTJSVariant getObjectContext(tTJSVariant value) {
        if (value.Type() != tvtObject) return tTJSVariant();
        iTJSDispatch2 *objthis = value.AsObjectClosureNoAddRef().ObjThis;
        return tTJSVariant(objthis, objthis);
    }

    static bool isNullContext(tTJSVariant value) {
        return value.Type() != tvtObject
            || value.AsObjectClosureNoAddRef().ObjThis == nullptr;
    }

    static bool equalStruct(tTJSVariant left, tTJSVariant right) {
        return EqualStruct(left, right, false);
    }

    static bool equalStructNumericLoose(tTJSVariant left, tTJSVariant right) {
        return EqualStruct(left, right, true);
    }

    static tjs_error TJS_INTF_METHOD getObjectKeys(tTJSVariant *result,
        tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *objthis) {
        if (numparams < 1) return TJS_E_BADPARAMCOUNT;
        if (result) *result = GetObjectKeys(*param[0]);
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD getObjectCount(tTJSVariant *result,
        tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *objthis) {
        if (numparams < 1) return TJS_E_BADPARAMCOUNT;
        if (result) *result = (tjs_int)GetObjectCount(*param[0]);
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD foreach(
        tTJSVariant *result, tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *objthis) {
        if (numparams < 2) return TJS_E_BADPARAMCOUNT;
        if (result) result->Clear();
        iTJSDispatch2 *target = ObjectOf(param[0]);
        if (!target) return TJS_S_OK;
        if (param[1]->Type() != tvtObject) return TJS_E_INVALIDPARAM;

        GuestFunction guest(param[1]->AsObjectClosureNoAddRef(), objthis, numparams, param);
        if (!guest.IsValid()) return TJS_E_INVALIDPARAM;

        if (IsArray(target)) {
            tjs_int count = TJSGetArrayElementCount(target);
            for (tjs_int i = 0; i < count; ++i) {
                tTJSVariant value;
                tjs_error hr = target->PropGetByNum(TJS_IGNOREPROP, i, &value, target);
                if (TJS_FAILED(hr)) return hr;
                tTJSVariant key(i);
                if (!guest.Call(key, value)) break;
            }
        } else {
            EnumForwarder forwarder(guest);
            tTJSVariantClosure callback(&forwarder, nullptr);
            tjs_error hr = target->EnumMembers(TJS_IGNOREPROP, &callback, target);
            if (TJS_FAILED(hr)) return hr;
        }
        if (result) *result = guest.Result;
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD getMD5HashString(tTJSVariant *result,
        tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *objthis) {
        if (numparams < 1) return TJS_E_BADPARAMCOUNT;
        if (param[0]->Type() != tvtOctet) return TJS_E_INVALIDPARAM;
        tTJSVariantOctet *octet = param[0]->AsOctetNoAddRef();
        if (!octet) return TJS_E_INVALIDPARAM;

        md5_state_t state;
        md5_init(&state);
        md5_append(&state, octet->GetData(), (int)octet->GetLength());
        tjs_uint8 digest[16];
        md5_finish(&state, digest);

        tjs_char text[33];
        static const tjs_char hex[] = TJS_W("0123456789abcdef");
        for (tjs_int i = 0; i < 16; ++i) {
            text[i * 2] = hex[(digest[i] >> 4) & 0xF];
            text[i * 2 + 1] = hex[digest[i] & 0xF];
        }
        text[32] = 0;
        if (result) *result = ttstr(text);
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD propSet(tTJSVariant *result,
        tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *objthis) {
        if (numparams < 3) return TJS_E_BADPARAMCOUNT;
        if (param[0]->Type() != tvtObject || param[1]->Type() == tvtObject)
            return TJS_E_INVALIDPARAM;
        tTJSVariantClosure closure = param[0]->AsObjectClosureNoAddRef();
        if (!closure.Object) return TJS_E_INVALIDPARAM;
        tjs_uint32 flag = numparams > 3
            ? (tjs_uint32)param[3]->operator tjs_int()
            : (tjs_uint32)TJS_MEMBERENSURE;
        if (param[1]->Type() == tvtInteger)
            return CallSafely([&]() {
                return closure.PropSetByNum(flag, param[1]->operator tjs_int(), param[2], nullptr);
            });
        return CallSafely([&]() {
            return closure.PropSet(
                flag, param[1]->GetString(), param[1]->GetHint(), param[2], nullptr);
        });
    }

    static tjs_error TJS_INTF_METHOD propGet(tTJSVariant *result,
        tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *objthis) {
        if (numparams < 2) return TJS_E_BADPARAMCOUNT;
        if (param[0]->Type() != tvtObject || param[1]->Type() == tvtObject)
            return TJS_E_INVALIDPARAM;
        tTJSVariantClosure closure = param[0]->AsObjectClosureNoAddRef();
        if (!closure.Object) return TJS_E_INVALIDPARAM;
        tjs_uint32 flag = numparams > 2
            ? (tjs_uint32)param[2]->operator tjs_int()
            : (tjs_uint32)TJS_MEMBERMUSTEXIST;
        if (param[1]->Type() == tvtInteger)
            return CallSafely([&]() {
                return closure.PropGetByNum(flag, param[1]->operator tjs_int(), result, nullptr);
            });
        return CallSafely([&]() {
            return closure.PropGet(
                flag, param[1]->GetString(), param[1]->GetHint(), result, nullptr);
        });
    }

    static tjs_error TJS_INTF_METHOD safeEvalStorage(tTJSVariant *result,
        tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *objthis) {
        if (numparams < 1) return TJS_E_BADPARAMCOUNT;

        ttstr name = *param[0];
        ttstr mode;
        if (numparams >= 2 && param[1]->Type() != tvtVoid) mode = *param[1];
        iTJSDispatch2 *context = numparams >= 3 && param[2]->Type() == tvtObject
            ? param[2]->AsObjectNoAddRef()
            : nullptr;

        iTJSTextReadStream *stream = TVPCreateTextStreamForRead(name, mode);
        ttstr buffer;
        try {
            stream->Read(buffer, 0);
        } catch (...) {
            stream->Destruct();
            throw;
        }
        stream->Destruct();

        ttstr expression(TJS_W("(const)["));
        expression += buffer;
        expression += TJS_W("]");

        tTJSVariant value;
        TVPExecuteExpression(expression, TVPExtractStorageName(name), 0, context, &value);
        if (result && value.Type() == tvtObject) {
            tTJSVariantClosure closure = value.AsObjectClosureNoAddRef();
            if (closure.Object)
                closure.PropGetByNum(TJS_IGNOREPROP, 0, result, nullptr);
        }
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD stringFuzzySearch(tTJSVariant *result,
        tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *objthis) {
        if (numparams < 3) return TJS_E_BADPARAMCOUNT;

        ttstr text = *param[0];
        ttstr pattern = *param[1];
        tjs_int max_ignores = *param[2];
        tjs_int char_bits =
            numparams > 3 && param[3]->Type() != tvtVoid ? (tjs_int)*param[3] : 7;
        tjs_int pattern_length = pattern.length();
        if (pattern_length >= 64 || max_ignores < 0 || char_bits <= 0 || char_bits > 16)
            return TJS_E_INVALIDPARAM;

        tjs_int index = pattern_length < 32
            ? BitapFuzzySearch<tjs_char, tjs_uint32>(text.c_str(), text.length(),
                  pattern.c_str(), pattern_length, max_ignores, char_bits)
            : BitapFuzzySearch<tjs_char, tjs_uint64>(text.c_str(), text.length(),
                  pattern.c_str(), pattern_length, max_ignores, char_bits);
        if (result) *result = index;
        return TJS_S_OK;
    }
};

} // namespace

NCB_ATTACH_CLASS(ScriptsExt, Scripts) {
    RawCallback(TJS_W("getObjectKeys"), &ScriptsExt::getObjectKeys, TJS_STATICMEMBER);
    RawCallback(TJS_W("getObjectCount"), &ScriptsExt::getObjectCount, TJS_STATICMEMBER);
    NCB_METHOD(getObjectContext);
    NCB_METHOD(isNullContext);
    NCB_METHOD(equalStruct);
    NCB_METHOD(equalStructNumericLoose);
    RawCallback(TJS_W("foreach"), &ScriptsExt::foreach, TJS_STATICMEMBER);
    RawCallback(TJS_W("getMD5HashString"), &ScriptsExt::getMD5HashString, TJS_STATICMEMBER);
    NCB_METHOD(clone);

    RawCallback(TJS_W("propSet"), &ScriptsExt::propSet, TJS_STATICMEMBER);
    RawCallback(TJS_W("propGet"), &ScriptsExt::propGet, TJS_STATICMEMBER);
    Variant(TJS_W("pfMemberEnsure"), TJS_MEMBERENSURE);
    Variant(TJS_W("pfMemberMustExist"), TJS_MEMBERMUSTEXIST);
    Variant(TJS_W("pfIgnoreProp"), TJS_IGNOREPROP);
    Variant(TJS_W("pfHiddenMember"), TJS_HIDDENMEMBER);
    Variant(TJS_W("pfStaticMember"), TJS_STATICMEMBER);

    RawCallback(TJS_W("safeEvalStorage"), &ScriptsExt::safeEvalStorage, TJS_STATICMEMBER);
    RawCallback(TJS_W("stringFuzzySearch"), &ScriptsExt::stringFuzzySearch, TJS_STATICMEMBER);
};

NCB_ATTACH_FUNCTION(rehash, Scripts, TJSDoRehash);
