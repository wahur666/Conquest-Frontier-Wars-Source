#include <iostream>

#include "DACOM.H"
#include "IProfileParser.h"
#include "TSmartPointer.h"

ICOManager *DACOM;

int main() {
    DACOM = DACOM_Acquire();
    DACOM->SetINIConfig("[Libraries]\r\nDOSFile.dll", DACOM_INI_STRING);
    COMPTR<IProfileParser> IPP;
    auto res = DACOM->QueryInterface( IID_IProfileParser, IPP.void_addr() );
    IPP->CreateSection("Library");

    std::cout << "Hello, World!" << res << std::endl;
    return 0;
}