#include <iostream>

#include "DACOM.H"
#include "IRustFunnyComponent.h"

ICOManager *DACOM;

int main() {
    DACOM = DACOM_Acquire();
    DACOM->SetINIConfig("[Libraries]\r\ncom_rom_rs.dll", DACOM_INI_STRING);

    ICOManager* dacom = DACOM_Acquire();
    void* pFunny = nullptr;

    DACOMDESC desc("IRustFunnyComponent");
    dacom->CreateInstance(&desc, &pFunny);

    IRustFunnyComponent* funny = (IRustFunnyComponent*)pFunny;
    int number = funny->GetTheFunnyNumber();  // Returns 69
    printf("Funny number: %d\n", number);

    char buffer[256];
    funny->ConcatenateStringWithInt("Hello", 42, buffer, sizeof(buffer));
    printf("Result: %s\n", buffer);  // Prints "Hello 42"

    funny->Release();

    std::cout << "Hello, World!" << std::endl;
    return 0;
}