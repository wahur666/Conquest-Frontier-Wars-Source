#include "DACOM.H"

#include <cstdio>

ICOManager* DACOM;

struct DACOM_NO_VTABLE ICSharpProbe : public IDAComponent
{
    virtual GENRESULT COMAPI Add(S32 left, S32 right, S32* result) = 0;
    virtual GENRESULT COMAPI Scale(SINGLE value, SINGLE factor, SINGLE* result) = 0;
    virtual GENRESULT COMAPI MakeGreeting(const C8* name, C8* buffer, U32 bufferSize) = 0;
    virtual GENRESULT COMAPI SumArray(const S32* values, U32 count, S32* result) = 0;
};

int main()
{
    DACOM = DACOM_Acquire();

    std::puts("CFW_Rebuild: loading CSharpDacomProbe.dll through DACOM");
    GENRESULT result = DACOM->SetINIConfig("[Libraries]\r\nCSharpDacomProbe.dll", DACOM_INI_STRING);
    if (result != GR_OK)
    {
        std::printf("CFW_Rebuild: SetINIConfig failed: %d\n", result);
        return 10;
    }

    DACOMDESC desc("ICSharpProbe");
    ICSharpProbe* probe = nullptr;
    result = DACOM->CreateInstance(&desc, reinterpret_cast<void**>(&probe));
    if (result != GR_OK || probe == nullptr)
    {
        std::printf("CFW_Rebuild: CreateInstance(ICSharpProbe) failed: %d\n", result);
        return 20;
    }

    std::puts("CFW_Rebuild: created ICSharpProbe");

    IDAComponent* queried = nullptr;
    result = probe->QueryInterface("ICSharpProbe", reinterpret_cast<void**>(&queried));
    if (result != GR_OK || queried == nullptr)
    {
        probe->Release();
        return 30;
    }

    queried->Release();

    S32 intResult = 0;
    result = probe->Add(37, 5, &intResult);
    if (result != GR_OK || intResult != 42)
    {
        std::printf("CFW_Rebuild: Add failed: result=%d value=%ld\n", result, intResult);
        probe->Release();
        return 40;
    }
    std::printf("CFW_Rebuild: Add round-trip -> %ld\n", intResult);

    SINGLE floatResult = 0.0f;
    result = probe->Scale(3.5f, 2.0f, &floatResult);
    if (result != GR_OK || floatResult != 7.0f)
    {
        std::printf("CFW_Rebuild: Scale failed: result=%d value=%f\n", result, floatResult);
        probe->Release();
        return 50;
    }
    std::printf("CFW_Rebuild: Scale round-trip -> %.2f\n", floatResult);

    C8 greeting[128] = {};
    result = probe->MakeGreeting("DACOM", greeting, sizeof(greeting));
    if (result != GR_OK)
    {
        std::printf("CFW_Rebuild: MakeGreeting failed: %d\n", result);
        probe->Release();
        return 60;
    }
    std::printf("CFW_Rebuild: MakeGreeting round-trip -> %s\n", greeting);

    S32 values[] = {1, 2, 3, 4, 5};
    S32 sum = 0;
    result = probe->SumArray(values, countof(values), &sum);
    if (result != GR_OK || sum != 15)
    {
        std::printf("CFW_Rebuild: SumArray failed: result=%d value=%ld\n", result, sum);
        probe->Release();
        return 70;
    }
    std::printf("CFW_Rebuild: SumArray round-trip -> %ld\n", sum);

    probe->Release();
    DACOM->ShutDown();
    std::puts("CFW_Rebuild: C# DACOM bridge smoke test passed");
    return 0;
}
