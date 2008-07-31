/** @file
 *
 * VBox frontends: VBoxManage (command-line interface), Guest Properties
 */

/*
 * Copyright (C) 2006-2007 Sun Microsystems, Inc.
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 USA or visit http://www.sun.com if you need
 * additional information or have any questions.
 */


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/

#include <VBox/com/com.h>
#include <VBox/com/string.h>
#include <VBox/com/array.h>
#include <VBox/com/ErrorInfo.h>

#include <VBox/com/VirtualBox.h>

#include <iprt/stream.h>
#include <VBox/log.h>

#include "VBoxManage.h"

using namespace com;

void usageGuestProperty(void)
{
    RTPrintf("VBoxManage guestproperty    get <vmname>|<uuid>\n"
             "                            <property> [-verbose]\n"
             "\n");
    RTPrintf("VBoxManage guestproperty    set <vmname>|<uuid>\n"
             "                            <property> [<value>] [-flags <flags>]\n"
             "\n");
    RTPrintf("VBoxManage guestproperty    enumerate <vmname>|<uuid>\n"
             "                            [-patterns <patterns>]\n"
             "\n");
}

static int handleGetGuestProperty(int argc, char *argv[],
                                  ComPtr<IVirtualBox> virtualBox,
                                  ComPtr<ISession> session)
{
    HRESULT rc = S_OK;

    bool verbose = false;
    if ((3 == argc) && (0 == strcmp(argv[2], "-verbose")))
        verbose = true;
    else if (argc != 2)
        return errorSyntax(USAGE_GUESTPROPERTY, "Incorrect parameters");

    ComPtr<IMachine> machine;
    /* assume it's a UUID */
    rc = virtualBox->GetMachine(Guid(argv[0]), machine.asOutParam());
    if (FAILED(rc) || !machine)
    {
        /* must be a name */
        CHECK_ERROR(virtualBox, FindMachine(Bstr(argv[0]), machine.asOutParam()));
    }
    if (machine)
    {
        Bstr value;
        uint64_t u64Timestamp;
        Bstr flags;
        CHECK_ERROR(machine, GetGuestProperty(Bstr(argv[1]), value.asOutParam(),
                    &u64Timestamp, flags.asOutParam()));
        if (!value)
            RTPrintf("No value set!\n");
        if (value)
            RTPrintf("Value: %lS\n", value.raw());
        if (value && verbose)
        {
            RTPrintf("Timestamp: %lld\n", u64Timestamp);
            RTPrintf("Flags: %lS\n", flags.raw());
        }
    }
    return SUCCEEDED(rc) ? 0 : 1;
}

static int handleSetGuestProperty(int argc, char *argv[],
                                  ComPtr<IVirtualBox> virtualBox,
                                  ComPtr<ISession> session)
{
    HRESULT rc = S_OK;

/*
 * Check the syntax.  We can deduce the correct syntax from the number of
 * arguments.
 */
    bool usageOK = true;
    const char *pszName = NULL;
    const char *pszValue = NULL;
    const char *pszFlags = NULL;
    if (3 == argc)
    {
        pszName = argv[1];
        pszValue = argv[2];
    }
    else if (4 == argc)
    {
        pszName = argv[1];
        if (strcmp(argv[2], "-flags") != 0)
            usageOK = false;
        pszFlags = argv[3];
    }
    else if (5 == argc)
    {
        pszName = argv[1];
        pszValue = argv[2];
        if (strcmp(argv[3], "-flags") != 0)
            usageOK = false;
        pszFlags = argv[4];
    }
    else if (argc != 2)
        usageOK = false;
    if (!usageOK)
        return errorSyntax(USAGE_GUESTPROPERTY, "Incorrect parameters");

    ComPtr<IMachine> machine;
    /* assume it's a UUID */
    rc = virtualBox->GetMachine(Guid(argv[0]), machine.asOutParam());
    if (FAILED(rc) || !machine)
    {
        /* must be a name */
        CHECK_ERROR(virtualBox, FindMachine(Bstr(argv[0]), machine.asOutParam()));
    }
    if (machine)
    {
        if ((NULL == pszValue) && (NULL == pszFlags))
            CHECK_ERROR(machine, SetGuestPropertyValue(Bstr(pszName), NULL));
        else if (NULL == pszFlags)
            CHECK_ERROR(machine, SetGuestPropertyValue(Bstr(pszName), Bstr(pszValue)));
        else if (NULL == pszValue)
            CHECK_ERROR(machine, SetGuestProperty(Bstr(pszName), NULL, Bstr(pszFlags)));
        else
            CHECK_ERROR(machine, SetGuestProperty(Bstr(pszName), Bstr(pszValue), Bstr(pszFlags)));
    }
    return SUCCEEDED(rc) ? 0 : 1;
}

/**
 * Enumerates the properties in the guest property store.
 *
 * @returns 0 on success, 1 on failure
 * @note see the command line API description for parameters
 */
static int handleEnumGuestProperty(int argc, char *argv[],
                                   ComPtr<IVirtualBox> aVirtualBox, ComPtr<ISession> aSession)
{
/*
 * Check the syntax.  We can deduce the correct syntax from the number of
 * arguments.
 */
    const char *paszPatterns = NULL;
    if ((argc > 2) && (0 == strcmp(argv[0], "-patterns")))
        paszPatterns = argv[1];
    else if (argc != 1)
        return errorSyntax(USAGE_GUESTPROPERTY, "Incorrect parameters");

/*
 * Count the size of the patterns and pack them.
 */
    size_t cchPatterns = 0;
    if (argc > 2)
        for (int i = 1; i < argc; ++i)
            cchPatterns += strlen(argv[i]) + 1;
    Utf8Str Utf8Patterns(cchPatterns);
    if ((cchPatterns > 0) && Utf8Patterns.isNull())
        return errorArgument ("out of memory");
    char *pszPatterns = Utf8Patterns.mutableRaw();
    size_t iPatterns = 0;
    if (argc > 2)
    {
        for (int i = 1; i < argc; ++i)
        {
            strcpy(pszPatterns + iPatterns, argv[i]);
            iPatterns += strlen(argv[i]) + 1;
            pszPatterns[iPatterns - 1] = ',';
        }
        pszPatterns[iPatterns - 1] = '\0';
    }

/*
 * Make the actual call to Main.
 */
    ComPtr<IMachine> machine;
    /* assume it's a UUID */
    HRESULT rc = aVirtualBox->GetMachine(Guid(argv[0]), machine.asOutParam());
    if (FAILED(rc) || !machine)
    {
        /* must be a name */
        CHECK_ERROR(aVirtualBox, FindMachine(Bstr(argv[0]), machine.asOutParam()));
    }
    if (machine)
    {
        com::SafeArray <BSTR> names;
        com::SafeArray <BSTR> values;
        com::SafeArray <ULONG64> timestamps;
        com::SafeArray <BSTR> flags;
        CHECK_ERROR(machine, EnumerateGuestProperties(Bstr(Utf8Patterns),
                                                      ComSafeArrayAsOutParam(names),
                                                      ComSafeArrayAsOutParam(values),
                                                      ComSafeArrayAsOutParam(timestamps),
                                                      ComSafeArrayAsOutParam(flags)));
        if (SUCCEEDED(rc))
        {
            if (names.size() == 0)
                RTPrintf("No properties found.\n");
            for (unsigned i = 0; i < names.size(); ++i)
                RTPrintf("Name: %lS, value: %lS, timestamp: %lld, flags: %lS\n",
                         names[i], values[i], timestamps[i], flags[i]);
        }
    }
    return SUCCEEDED(rc) ? 0 : 1;
}

/**
 * Access the guest property store.
 *
 * @returns 0 on success, 1 on failure
 * @note see the command line API description for parameters
 */
int handleGuestProperty(int argc, char *argv[],
                        ComPtr<IVirtualBox> aVirtualBox, ComPtr<ISession> aSession)
{
    if (0 == argc)
        return errorSyntax(USAGE_GUESTPROPERTY, "Incorrect parameters");
    if (0 == strcmp(argv[0], "get"))
        return handleGetGuestProperty(argc - 1, argv + 1, aVirtualBox, aSession);
    else if (0 == strcmp(argv[0], "set"))
        return handleSetGuestProperty(argc - 1, argv + 1, aVirtualBox, aSession);
    else if (0 == strcmp(argv[0], "enumerate"))
        return handleEnumGuestProperty(argc - 1, argv + 1, aVirtualBox, aSession);
    /* else */
    return errorSyntax(USAGE_GUESTPROPERTY, "Incorrect parameters");
}

