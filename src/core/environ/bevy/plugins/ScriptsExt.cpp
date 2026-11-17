// Scripts.foreach(collection, callback, ...arguments) and Scripts.getObjectKeys.
// Callbacks receive (key, value, ...arguments); a non-void result stops iteration.
#include "ncbind/ncbind.hpp"

#include <vector>

#include "tjsArray.h"
#include "tjsDictionary.h"
#include "tjsNative.h"

namespace {

class GuestFunction {
    tTJSVariantClosure Closure;
    std::vector<tTJSVariant *> Arguments;

public:
    tTJSVariant Result;

    GuestFunction(const tTJSVariantClosure &function, iTJSDispatch2 *thisObj,
        tjs_int count, tTJSVariant **params) : Arguments(params, params + count) {
        Closure.Object = function.Object;
        Closure.ObjThis = function.ObjThis ? function.ObjThis : thisObj;
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
        if (numparams >= 3 && !(param[1]->AsInteger() & TJS_HIDDENMEMBER))
            next = Guest.Call(*param[0], *param[2]);
        if (result) *result = static_cast<tjs_int>(next);
        return TJS_S_OK;
    }
};

bool IsArray(iTJSDispatch2 *obj) {
    iTJSNativeInstance *instance = nullptr;
    return TJS_SUCCEEDED(obj->NativeInstanceSupport(
        TJS_NIS_GETINSTANCE, (tjs_uint32)TJSGetArrayClassID(), &instance));
}

// A void collection has no members.
iTJSDispatch2 *ObjectOf(const tTJSVariant *value) {
    return value && value->Type() == tvtObject ? value->AsObjectNoAddRef() : nullptr;
}

class NameCollector : public tTJSNativeFunction {
public:
    std::vector<ttstr> Names;

    NameCollector() : tTJSNativeFunction(TJS_W("Scripts.getObjectKeys")) {}

    tjs_error TJS_INTF_METHOD Process(tTJSVariant *result, tjs_int numparams,
        tTJSVariant **param, iTJSDispatch2 *objthis) override {
        // A falsy result ends the enumeration.
        if (result) *result = (tjs_int)1;
        if (numparams >= 1) Names.push_back(ttstr(*param[0]));
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
        if (numparams < 3) return TJS_E_BADPARAMCOUNT;
        tTJSVariant value = Clone(*param[2]);
        tjs_error hr = Target->PropSet(TJS_MEMBERENSURE | static_cast<tjs_int>(*param[1]),
            param[0]->GetString(), nullptr, &value, Target);
        if (result) *result = static_cast<tjs_int>(1);
        return hr;
    }
};

tTJSVariant Clone(const tTJSVariant &value) {
    iTJSDispatch2 *object = ObjectOf(&value);
    if (!object) return value;
    auto &closure = value.AsObjectClosureNoAddRef();
    if (IsArray(object)) {
        iTJSDispatch2 *target = TJSCreateArrayObject();
        tTJSVariant result(target, target);
        target->Release();
        tjs_int count = TJSGetArrayElementCount(object);
        for (tjs_int i = 0; i < count; ++i) {
            tTJSVariant member;
            tjs_error hr = closure.PropGetByNum(TJS_IGNOREPROP, i, &member, nullptr);
            if (TJS_FAILED(hr)) TJSThrowFrom_tjs_error(hr);
            member = Clone(member);
            hr = target->PropSetByNum(TJS_MEMBERENSURE, i, &member, target);
            if (TJS_FAILED(hr)) TJSThrowFrom_tjs_error(hr);
        }
        return result;
    }
    if (closure.IsInstanceOf(0, nullptr, nullptr, TJS_W("Dictionary"), nullptr) == TJS_S_TRUE) {
        iTJSDispatch2 *target = TJSCreateDictionaryObject();
        tTJSVariant result(target, target);
        target->Release();
        CloneMember member(target);
        tTJSVariantClosure callback(&member, nullptr);
        tjs_error hr = closure.EnumMembers(TJS_IGNOREPROP, &callback, nullptr);
        if (TJS_FAILED(hr)) TJSThrowFrom_tjs_error(hr);
        return result;
    }
    tTJSVariant result;
    if (closure.FuncCall(0, TJS_W("clone"), nullptr, &result, 0, nullptr, nullptr) == TJS_S_TRUE)
        return result;
    return value;
}

struct ScriptsExt {
    static tTJSVariant clone(tTJSVariant value) { return Clone(value); }

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

    static tjs_error TJS_INTF_METHOD getObjectKeys(
        tTJSVariant *result, tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *objthis) {
        if (numparams < 1) return TJS_E_BADPARAMCOUNT;
        NameCollector collector;
        tjs_int array_count = -1;
        if (iTJSDispatch2 *target = ObjectOf(param[0])) {
            if (IsArray(target)) {
                // Array elements do not implement EnumMembers.
                array_count = TJSGetArrayElementCount(target);
            } else {
                tTJSVariantClosure callback(&collector, nullptr);
                // Enumerating names must not invoke property getters.
                tjs_error hr = target->EnumMembers(TJS_ENUM_NO_VALUE, &callback, target);
                if (TJS_FAILED(hr) && hr != TJS_E_NOTIMPL) return hr;
            }
        }

        iTJSDispatch2 *array_class = nullptr;
        iTJSDispatch2 *array = TJSCreateArrayObject(&array_class);
        tTJSVariant value;
        try {
            tjs_int count = array_count >= 0 ? array_count : (tjs_int)collector.Names.size();
            for (tjs_int i = 0; i < count; ++i) {
                value = array_count >= 0 ? tTJSVariant(i) : tTJSVariant(collector.Names[i].c_str());
                tjs_error set = array->PropSetByNum(TJS_MEMBERENSURE, i, &value, array);
                if (TJS_FAILED(set)) TJSThrowFrom_tjs_error(set, TJS_W("Scripts.getObjectKeys"));
            }
        } catch (...) {
            array->Release();
            if (array_class) array_class->Release();
            throw;
        }
        if (result) *result = tTJSVariant(array, array);
        array->Release();
        if (array_class) array_class->Release();
        return TJS_S_OK;
    }
};

} // namespace

#define NCB_MODULE_NAME TJS_W("scriptsExt.dll")

NCB_ATTACH_CLASS(ScriptsExt, Scripts) {
    NCB_METHOD(clone);
    RawCallback("foreach", &ScriptsExt::foreach, TJS_STATICMEMBER);
    RawCallback("getObjectKeys", &ScriptsExt::getObjectKeys, TJS_STATICMEMBER);
}
