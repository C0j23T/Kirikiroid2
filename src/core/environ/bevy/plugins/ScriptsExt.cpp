// bevy port: `Scripts.foreach` and `Scripts.getObjectKeys`.
//
// Both come from KAGParserEx.dll, the KAG parser extension yuzusoft ships with
// their games; it is closed source and not part of this checkout. KAG's own
// system scripts use them heavily (`Scripts.foreach(objects, function(o) ...)`
// in world.tjs/KAGEnvBase, `Scripts.getObjectKeys` everywhere around it), so
// without them a game's `startup.tjs` cannot get as far as its first screen.
//
// Why the engine has to provide them at all: TJS2 has no `for (var x in obj)`
// statement, so enumerating an object's members is only possible through
// `iTJSDispatch2::EnumMembers`, and arrays do not even implement that (see
// tjsArray.h) -- they are walked by index here.
//
// `foreach` takes `(collection, function, this = void)`; arrays are walked as
// `callback(value, index)`, other objects as `callback(value, name)`.
#include "ncbind/ncbind.hpp"

#include <vector>

#include "tjsArray.h"
#include "tjsNative.h"

namespace {

/// The TJS function a `foreach` call has to invoke, with the `this` it was
/// given (kept alive for as long as the caller does).
class GuestFunction {
    tTJSVariantClosure Closure;

public:
    GuestFunction(const tTJSVariantClosure &function, iTJSDispatch2 *thisObj) {
        Closure.Object = function.Object;
        Closure.ObjThis = thisObj ? thisObj : function.ObjThis;
        Closure.AddRef();
    }
    ~GuestFunction() { Closure.Release(); }
    GuestFunction(const GuestFunction &) = delete;
    GuestFunction &operator=(const GuestFunction &) = delete;

    bool IsValid() const { return Closure.Object != nullptr; }

    /// Calls it with `(value, key)`; nothing else is passed.
    void Call(const tTJSVariant &value, const tTJSVariant &key) {
        tTJSVariant args[2] = {value, key};
        tTJSVariant *ptrs[2] = {&args[0], &args[1]};
        tTJSVariant result;
        tjs_error hr = Closure.FuncCall(0, nullptr, nullptr, &result, 2, ptrs, nullptr);
        if (TJS_FAILED(hr)) TJSThrowFrom_tjs_error(hr, TJS_W("Scripts.foreach"));
    }
};

/// Bridges `EnumMembers` (which reports `(name, flags, value)`) to the guest
/// function.
class EnumForwarder : public tTJSNativeFunction {
    GuestFunction &Guest;

public:
    explicit EnumForwarder(GuestFunction &guest)
        : tTJSNativeFunction(TJS_W("Scripts.foreach")), Guest(guest) {}

    tjs_error TJS_INTF_METHOD Process(tTJSVariant *result, tjs_int numparams,
        tTJSVariant **param, iTJSDispatch2 *objthis) override {
        // A falsy result makes `EnumMembers` stop after this member, so
        // returning void here would visit only the first one.
        if (result) *result = (tjs_int)1;
        if (numparams >= 3) Guest.Call(*param[2], *param[0]);
        return TJS_S_OK;
    }
};

/// True for a TJS `Array` object (the only collection type that cannot be
/// enumerated member-wise).
bool IsArray(iTJSDispatch2 *obj) {
    iTJSNativeInstance *instance = nullptr;
    return TJS_SUCCEEDED(obj->NativeInstanceSupport(
        TJS_NIS_GETINSTANCE, (tjs_uint32)TJSGetArrayClassID(), &instance));
}

/// `AsObjectNoAddRef` throws on a non-object, and KAG calls these with a void
/// `this`/collection all the time, so check the type first.
iTJSDispatch2 *ObjectOf(const tTJSVariant *value) {
    return value && value->Type() == tvtObject ? value->AsObjectNoAddRef() : nullptr;
}

/// Collects the member names `EnumMembers` reports.
class NameCollector : public tTJSNativeFunction {
public:
    std::vector<ttstr> Names;

    NameCollector() : tTJSNativeFunction(TJS_W("Scripts.getObjectKeys")) {}

    tjs_error TJS_INTF_METHOD Process(tTJSVariant *result, tjs_int numparams,
        tTJSVariant **param, iTJSDispatch2 *objthis) override {
        // See EnumForwarder::Process: a falsy result ends the enumeration.
        if (result) *result = (tjs_int)1;
        if (numparams >= 1) Names.push_back(ttstr(*param[0]));
        return TJS_S_OK;
    }
};

// ── the members added to the Scripts class ─────────────────────────────────

struct ScriptsExt {
    /// `Scripts.foreach(collection, function(value, key), this = void)`
    static tjs_error TJS_INTF_METHOD foreach(
        tTJSVariant *result, tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *objthis) {
        if (numparams < 2) return TJS_E_BADPARAMCOUNT;
        iTJSDispatch2 *target = ObjectOf(param[0]);
        if (!target) return TJS_S_OK; // iterating a non-object yields nothing
        if (param[1]->Type() != tvtObject) return TJS_E_INVALIDPARAM;
        GuestFunction guest(param[1]->AsObjectClosureNoAddRef(),
            numparams >= 3 ? ObjectOf(param[2]) : nullptr);
        if (!guest.IsValid()) return TJS_E_INVALIDPARAM;

        if (IsArray(target)) {
            tjs_int count = TJSGetArrayElementCount(target);
            for (tjs_int i = 0; i < count; ++i) {
                tTJSVariant value;
                target->PropGetByNum(0, i, &value, target);
                tTJSVariant key(i);
                guest.Call(value, key);
            }
            return TJS_S_OK;
        }
        EnumForwarder forwarder(guest);
        tTJSVariantClosure callback(&forwarder, nullptr);
        // Flags 0, not `TJS_IGNOREPROP`: a member that is reached through a
        // property call (KAG's `names` dictionaries are full of them) fails to
        // read with property invoking off, and `EnumMembers` then stops after
        // the first member without reporting anything.
        return target->EnumMembers(0, &callback, target);
    }

    /// `Scripts.getObjectKeys(collection)` -> array of the keys `foreach`
    /// would pass, in the same order: `0..length-1` for an array, the member
    /// names otherwise.
    static tjs_error TJS_INTF_METHOD getObjectKeys(
        tTJSVariant *result, tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *objthis) {
        if (numparams < 1) return TJS_E_BADPARAMCOUNT;
        NameCollector collector;
        tjs_int array_count = -1;
        if (iTJSDispatch2 *target = ObjectOf(param[0])) {
            if (IsArray(target)) {
                // Nothing to enumerate: an array's own elements are not
                // members, and its `EnumMembers` reports nothing (tjsArray.h
                // says "currently not implemented"). Its keys are its indices,
                // which is also what `foreach` hands the callback.
                array_count = TJSGetArrayElementCount(target);
            } else {
                tTJSVariantClosure callback(&collector, nullptr);
                // `TJS_ENUM_NO_VALUE`: only the names are wanted, and not
                // reading the members keeps a member that is reached through a
                // property call from aborting the walk (see `foreach` above).
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

#define NCB_MODULE_NAME TJS_W("bevy_scripts.dll")

NCB_ATTACH_CLASS(ScriptsExt, Scripts) {
    RawCallback("foreach", &ScriptsExt::foreach, TJS_STATICMEMBER);
    RawCallback("getObjectKeys", &ScriptsExt::getObjectKeys, TJS_STATICMEMBER);
}
