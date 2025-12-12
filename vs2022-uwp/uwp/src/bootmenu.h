#ifndef BOOTMENU_H
#define BOOTMENU_H

namespace bootmenu
{
    // Boot menu that handles ROM selection and extraction before game initialization
    // Returns true if boot should continue, false if user cancelled
    bool BootSelect(void* wnd, int w, int h);
}

#endif // BOOTMENU_H






