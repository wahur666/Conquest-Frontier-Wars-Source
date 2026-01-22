// IRustFunnyComponent.h
#pragma once

// struct IRustFunnyComponentVTable;
//
// struct IRustFunnyComponent
// {
//     IRustFunnyComponentVTable* vtable;
//
//     // IDAComponent methods
//     int QueryInterface(const char* iid, void** instance);
//     int AddRef();
//     int Release();
//     int QueryOutgoingInterface(const char* name, void** connection);
//
//     // IRustFunnyComponent specific
//     int GetTheFunnyNumber();
//     int ConcatenateStringWithInt(const char* input, int value, char* output, unsigned int buf_size);
// };
//
// struct IRustFunnyComponentVTable
// {
//     int (__stdcall *QueryInterface)(IRustFunnyComponent*, const unsigned char*, void**);
//     unsigned int (__stdcall *AddRef)(IRustFunnyComponent*);
//     unsigned int (__stdcall *Release)(IRustFunnyComponent*);
//     int (__stdcall *QueryOutgoingInterface)(IRustFunnyComponent*, const unsigned char*, void**);
//     int (__stdcall *GetTheFunnyNumber)(IRustFunnyComponent*);
//     int (__stdcall *ConcatenateStringWithInt)(IRustFunnyComponent*, const unsigned char*, int, unsigned char*, unsigned int);
// };
//
// // Out-of-line implementations
// inline int IRustFunnyComponent::QueryInterface(const char* iid, void** instance) {
//     return vtable->QueryInterface(this, (const unsigned char*)iid, instance);
// }
//
// inline int IRustFunnyComponent::AddRef() {
//     return vtable->AddRef(this);
// }
//
// inline int IRustFunnyComponent::Release() {
//     return vtable->Release(this);
// }
//
// inline int IRustFunnyComponent::QueryOutgoingInterface(const char* name, void** connection) {
//     return vtable->QueryOutgoingInterface(this, (const unsigned char*)name, connection);
// }
//
// inline int IRustFunnyComponent::GetTheFunnyNumber() {
//     return vtable->GetTheFunnyNumber(this);
// }
//
// inline int IRustFunnyComponent::ConcatenateStringWithInt(const char* input, int value, char* output, unsigned int buf_size) {
//     return vtable->ConcatenateStringWithInt(this, (const unsigned char*)input, value, (unsigned char*)output, buf_size);
// }

struct IRustFunnyComponent;
struct IRustFunnyComponentVTable {
    int (__stdcall *QueryInterface)(IRustFunnyComponent*, const char*, void**);
    unsigned int (__stdcall *AddRef)(IRustFunnyComponent*);
    unsigned int (__stdcall *Release)(IRustFunnyComponent*);
    int (__stdcall *QueryOutgoingInterface)(IRustFunnyComponent*, const char*, IDAConnectionPoint**);
    int (__stdcall *GetTheFunnyNumber)(IRustFunnyComponent*);
    int (__stdcall *ConcatenateStringWithInt)(IRustFunnyComponent*, const char*, int, char*, unsigned int);
};

struct IRustFunnyComponent {
    IRustFunnyComponentVTable* vtable;

    int QueryInterface(const char* arg0, void** arg1) {
        return vtable->QueryInterface(this, arg0, arg1);
    }

    unsigned int AddRef() {
        return vtable->AddRef(this);
    }

    unsigned int Release() {
        return vtable->Release(this);
    }

    int QueryOutgoingInterface(const char* arg0, IDAConnectionPoint** arg1) {
        return vtable->QueryOutgoingInterface(this, arg0, arg1);
    }

    int GetTheFunnyNumber() {
        return vtable->GetTheFunnyNumber(this);
    }

    int ConcatenateStringWithInt(const char* arg0, int arg1, char* arg2, unsigned int arg3) {
        return vtable->ConcatenateStringWithInt(this, arg0, arg1, arg2, arg3);
    }

};
