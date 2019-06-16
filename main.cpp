#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define COM_NO_WINDOWS_H
#include <combaseapi.h>
#include <PortableDeviceApi.h>
#include <PortableDevice.h>
#include <PathCch.h>
#include <Shlwapi.h>
#include <new>
#include <stdio.h>
#include <assert.h>
#include <wchar.h>
#include <stdarg.h>

#pragma comment(lib, "PortableDeviceGUIDs.lib")
#pragma comment(lib, "PathCch.lib")
#pragma comment(lib, "Shlwapi.lib")

struct Args {
    bool ok = false;
    wchar_t* device_friendly_name = nullptr;
    wchar_t* device_description = nullptr;
    wchar_t* match = nullptr;
    wchar_t* source_directory = nullptr;
    wchar_t* destination_directory = nullptr;
    bool list_devices = false;
    bool copy_files = false;
    bool delete_files = false;
    bool list_files = false;
};

struct PortableDeviceInformation {
    wchar_t* id = nullptr;
    wchar_t* friendly_name = nullptr;
    wchar_t* description = nullptr;
};

struct DeviceObjectInformation {
    wchar_t* id = nullptr;
    wchar_t* name = nullptr;
    HRESULT hr = E_FAIL;
};

template<typename T>
static void safe_release(T** ptr) {
    if (*ptr) {
        (*ptr)->Release();
        *ptr = nullptr;
    }
}

static wchar_t* string_clone(const wchar_t* src, int length = -1) {
    if (!src) return nullptr;
    if (length < 0) {
        length = (int)wcslen(src);
    }
    wchar_t* dst = new (std::nothrow) wchar_t[length + 1];
    if (dst) {
        memcpy(dst, src, sizeof(wchar_t) * length);
        dst[length] = L'\0';
    }
    return dst;
}

static wchar_t* string_format(const wchar_t* format, ...) {
    va_list args;
    va_start(args, format);
    int buffer_count = _vscwprintf(format, args);
    va_end(args);
    if (buffer_count < 0) {
        return nullptr;
    }

    wchar_t* buffer = new (std::nothrow) wchar_t[buffer_count + 1];
    if (!buffer) {
        return nullptr;
    }

    va_start(args, format);
    int count = _vsnwprintf_s(buffer, buffer_count + 1, buffer_count, format, args);
    va_end(args);
    if (count < 0) {
        delete[] buffer;
        return nullptr;
    }

    buffer[buffer_count] = L'\0';
    return buffer;
}

static wchar_t* hresult_to_string(HRESULT hr) {
    wchar_t* error = nullptr;
    DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        hr,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (wchar_t*)&error,
        0,
        nullptr);

    wchar_t* result;
    if (error) {
        if (length >= 2 && error[length - 2] == '\r' && error[length - 1] == '\n') {
            length -= 2;
        }
        result = string_clone(error, (int)length);
        LocalFree(error);
    } else {
        result = string_format(L"Unknown error 0x%0lX", hr);
        if (!result) {
            result = string_clone(L"Unknown error");
        }
    }
    return result;
}

static Args parse_args(int argc, wchar_t** argv) {
    Args args;
    const wchar_t* error = nullptr;

    for (int i = 1; i < argc; ++i) {
        wchar_t* arg = argv[i];
        if (wcslen(arg) <= 2 || arg[0] != '-' || arg[1] != '-') {
            error = string_format(L"Unknown argument \"%s\"", arg);
            goto on_error;
        }
        wchar_t* name = &arg[2];

        {
            bool* field = nullptr;

            if (0 == wcscmp(name, L"list_devices")) {
                field = &args.list_devices;
            } else if (0 == wcscmp(name, L"copy_files")) {
                field = &args.copy_files;
            } else if (0 == wcscmp(name, L"delete_files")) {
                field = &args.delete_files;
            } else if (0 == wcscmp(name, L"list_files")) {
                field = &args.list_files;
            }

            if (field) {
                *field = true;
                continue;
            }
        }

        {
            wchar_t** field = nullptr;

            if (0 == wcscmp(name, L"device_description")) {
                field = &args.device_description;
            } else if (0 == wcscmp(name, L"source_directory")) {
                field = &args.source_directory;
            } else if (0 == wcscmp(name, L"destination_directory")) {
                field = &args.destination_directory;
            } else if (0 == wcscmp(name, L"match")) {
                field = &args.match;
            }

            if (field == nullptr) {
                error = string_format(L"Unknown argument \"--%s\"", name);
                goto on_error;
            }
            
            if (i + 1 >= argc) {
                error = string_format(L"Value of argument \"--%s\" is not set", name);
                goto on_error;
            }
            wchar_t* value = argv[i + 1];
            ++i;

            delete[] (*field);
            *field = string_clone(value);
            if (!(*field)) {
                error = L"Out of memory";
                goto on_error;
            }
        }
    }

    if (!args.list_devices) {
        if (!args.device_friendly_name && !args.device_description) {
            error = L"Neither device friendly name nor description is not set.\n";
            goto on_error;
        }

        if (args.copy_files && !args.source_directory) {
            error = L"Source directory is not set.\n";
            goto on_error;
        }

        if (args.copy_files && !args.destination_directory) {
            error = L"Source directory is not set.\n";
            goto on_error;
        }

        if (!args.copy_files && !args.delete_files && !args.list_files) {
            error = L"Action is not set (specify --copy_files, --delete_files or --list_files).\n";
            goto on_error;
        }

        if (args.list_files && (args.copy_files || args.delete_files)) {
            error = L"--list_files cannot be used together with --copy_files or --delete_files\n";
            goto on_error;
        }
    }

    if (error) {
        on_error:
        args.ok = false;
        wprintf(L"Error: %s\n", error ? error : L"<out of memory>");
    } else {
        args.ok = true;
    }

    return args;
}

// --- deallocate with CoTaskMemFree ---
// ^^^^^^^^^^^^ FIX THIS @TODO
static HRESULT get_device_object_name(IPortableDeviceProperties* properties, const wchar_t* object_id, wchar_t** out_object_name) {
    HRESULT hr = E_FAIL;
    *out_object_name = nullptr;

    static IPortableDeviceKeyCollection* original_name_keys = nullptr;
    if (!original_name_keys) {
        hr = CoCreateInstance(CLSID_PortableDeviceKeyCollection, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&original_name_keys));
        if (FAILED(hr)) return hr;
        hr = original_name_keys->Add(WPD_OBJECT_ORIGINAL_FILE_NAME);
        if (FAILED(hr)) {
            safe_release(&original_name_keys);
            return hr;
        }
    }

    static IPortableDeviceKeyCollection* name_keys = nullptr;
    if (!name_keys) {
        hr = CoCreateInstance(CLSID_PortableDeviceKeyCollection, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&name_keys));
        if (FAILED(hr)) return hr;
        hr = name_keys->Add(WPD_OBJECT_NAME);
        if (FAILED(hr)) {
            safe_release(&name_keys);
            return hr;
        }
    }

    // ORIGINAL_FILE_NAME = with file extension
    // NAME = without file extension

    IPortableDeviceValues* values = nullptr;

    // Try get file name with extension.
    hr = properties->GetValues(object_id, original_name_keys, &values);
    if (SUCCEEDED(hr)) {
        wchar_t* original_name = nullptr;
        hr = values->GetStringValue(WPD_OBJECT_ORIGINAL_FILE_NAME, &original_name);
        if (SUCCEEDED(hr)) {
            *out_object_name = string_clone(original_name);
            if (!out_object_name) {
                hr = E_OUTOFMEMORY;
            }
            CoTaskMemFree(original_name);
        }
    } 

    // If failed, get normal name.
    if (FAILED(hr) && hr != E_OUTOFMEMORY) {
        safe_release(&values);
        hr = properties->GetValues(object_id, name_keys, &values);
        if (SUCCEEDED(hr)) {
            wchar_t* name = nullptr;
            hr = values->GetStringValue(WPD_OBJECT_NAME, &name);
            if (SUCCEEDED(hr)) {
                *out_object_name = string_clone(name);
                if (!out_object_name) {
                    hr = E_OUTOFMEMORY;
                }
                CoTaskMemFree(name);
            }
        }
    }

    safe_release(&values);
    return hr;
}

static HRESULT enumerate_devices(PortableDeviceInformation** out_devices, int* out_ndevices) {
    assert(out_devices);
    assert(out_ndevices);

    IPortableDeviceManager* device_manager = nullptr;
    wchar_t** device_ids = nullptr;
    DWORD ndevices = 0;
    PortableDeviceInformation* devices = nullptr;
    HRESULT hr = E_FAIL;

    hr = CoCreateInstance(CLSID_PortableDeviceManager, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&device_manager));
    if (FAILED(hr)) goto quit;

    hr = device_manager->GetDevices(nullptr, &ndevices);
    if (FAILED(hr)) goto quit;

    device_ids = new (std::nothrow) wchar_t*[ndevices];
    if (!device_ids) {
        hr = E_OUTOFMEMORY;
        goto quit;
    }
    
    hr = device_manager->GetDevices(device_ids, &ndevices);
    if (FAILED(hr)) goto quit;

    devices = new (std::nothrow) PortableDeviceInformation[ndevices];
    if (!device_ids) {
        hr = E_OUTOFMEMORY;
        goto quit;
    }

    for (DWORD i = 0; i < ndevices; ++i) {
        auto& device = devices[i];

        // Identifier.
        device.id = string_clone(device_ids[i]);
        if (!device.id) {
            hr = E_OUTOFMEMORY;
            goto quit;
        }

        // HRESULT_FROM_WIN32(ERROR_INVALID_DATA)
        // means that device friendly name/description is not set.

        // Friendly name.
        {
            DWORD nfriendly_name = 0;
            hr = device_manager->GetDeviceFriendlyName(device.id, nullptr, &nfriendly_name);
            if (hr != HRESULT_FROM_WIN32(ERROR_INVALID_DATA)) {
                if (FAILED(hr)) goto quit;

                device.friendly_name = new (std::nothrow) wchar_t[nfriendly_name + 1];
                if (!device.friendly_name) {
                    hr = E_OUTOFMEMORY;
                    goto quit;
                }

                hr = device_manager->GetDeviceFriendlyName(device.id, device.friendly_name, &nfriendly_name);
                if (FAILED(hr)) goto quit;
                device.friendly_name[nfriendly_name] = L'\0';
            }
        }
    
        // Description.
        {
            DWORD ndescription = 0;
            hr = device_manager->GetDeviceDescription(device.id, nullptr, &ndescription);
            if (hr != HRESULT_FROM_WIN32(ERROR_INVALID_DATA)) {
                if (FAILED(hr)) goto quit;

                device.description = new (std::nothrow) wchar_t[ndescription + 1];
                if (!device.description) {
                    hr = E_OUTOFMEMORY;
                    goto quit;
                }

                hr = device_manager->GetDeviceDescription(device.id, device.description, &ndescription);
                if (FAILED(hr)) goto quit;
                device.description[ndescription] = L'\0';
            }
        }
    }

    hr = S_OK;

    quit:
    if (device_ids) {
        for (DWORD i = 0; i < ndevices; ++i) {
            CoTaskMemFree(device_ids[i]);
        }
        delete[] device_ids;
    }
    if (devices && FAILED(hr)) {
        for (DWORD i = 0; i < ndevices; ++i) {
            auto device = devices[i];
            delete[] device.id;
            delete[] device.friendly_name;
            delete[] device.description;
        }
        delete[] devices;
    }
    safe_release(&device_manager);
    *out_devices = SUCCEEDED(hr) ? devices : nullptr;
    *out_ndevices = SUCCEEDED(hr) ? (int)ndevices : 0;
    return hr;
}

static PortableDeviceInformation* match_device(PortableDeviceInformation* devices, int ndevices, const Args& args) {
    assert(devices);

    for (int i = 0; i < ndevices; ++i) {
        auto& device = devices[i];

        if (device.description && args.device_description && 0 == _wcsicmp(device.description, args.device_description)) {
            return &device;
        }
    }

    return nullptr;
}

static HRESULT find_device_object(IPortableDeviceContent* content, IPortableDeviceProperties* properties, const wchar_t* parent_object_id, const wchar_t* search_object_name, wchar_t** out_object_id) {
    const int BatchSize = 32;
    HRESULT hr = E_FAIL;
    IEnumPortableDeviceObjectIDs* enumerator = nullptr;
    DWORD nfetched = 0;
    wchar_t* object_ids[BatchSize] = { 0 };
    wchar_t* target_object_id = nullptr;

    hr = content->EnumObjects(0, parent_object_id, nullptr, &enumerator);
    if (FAILED(hr)) goto quit;
    if (hr != S_OK) {
        hr = E_FAIL;
        goto quit;
    }

    do {
        hr = enumerator->Next(BatchSize, object_ids, &nfetched);
        if (SUCCEEDED(hr)) {
            for (DWORD i = 0; i < nfetched; ++i) {
                wchar_t* object_id = object_ids[i];
                wchar_t* object_name = nullptr;

                hr = get_device_object_name(properties, object_id, &object_name);
                if (FAILED(hr)) goto quit;

                if (0 == _wcsicmp(object_name, search_object_name)) {
                    target_object_id = string_clone(object_id);
                    if (!target_object_id) {
                        hr = E_OUTOFMEMORY;
                    }
                }

                delete[] object_name;
                CoTaskMemFree(object_id);
                object_ids[i] = nullptr;

                if (FAILED(hr)) {
                    delete[] target_object_id;
                    goto quit;
                }

                if (target_object_id) {
                    goto quit;
                }
            }
            nfetched = 0;
        }
    } while (hr == S_OK);

    quit:
    safe_release(&enumerator);
    if (FAILED(hr)) {
        delete[] target_object_id;
        target_object_id = nullptr;
    }
    for (DWORD i = 0; i < nfetched; ++i) {
        CoTaskMemFree(object_ids[i]);
    }
    *out_object_id = SUCCEEDED(hr) ? target_object_id : nullptr;
    return hr;
}

static HRESULT enumerate_device_objects(
    IPortableDeviceContent* content, 
    IPortableDeviceProperties* properties, 
    const wchar_t* object_id,
    DeviceObjectInformation** out_objectinfo, 
    int* out_nobjectinfo,
    void* userdata,
    bool(*filter)(const wchar_t* object_name, void* userdata))
{
    const int BatchSize = 32;
    HRESULT hr = E_FAIL;
    IEnumPortableDeviceObjectIDs* enumerator = nullptr;
    DWORD nfetched = 0;
    wchar_t* object_ids[BatchSize] = { 0 };
    DeviceObjectInformation* objectinfo = nullptr;
    int objectinfo_count = 0;
    int objectinfo_capacity = 0;
    wchar_t* current_object_name = nullptr;

    hr = content->EnumObjects(0, object_id, nullptr, &enumerator);
    if (FAILED(hr)) goto quit;
    if (hr != S_OK) {
        hr = E_FAIL;
        goto quit;
    }

    do {
        hr = enumerator->Next(BatchSize, object_ids, &nfetched);
        if (SUCCEEDED(hr)) {
            for (DWORD i = 0; i < nfetched; ++i) {
                wchar_t* object_id = object_ids[i];

                hr = get_device_object_name(properties, object_id, &current_object_name);
                if (FAILED(hr)) goto quit;

                if (filter(current_object_name, userdata)) {
                    if (objectinfo == nullptr || objectinfo_count + 1 >= objectinfo_capacity) {
                        int new_capacity = objectinfo_capacity == 0 ? BatchSize : objectinfo_capacity * 2;
                        DeviceObjectInformation* new_objectinfo = new (std::nothrow) DeviceObjectInformation[new_capacity];
                        if (!new_objectinfo) {
                            hr = E_OUTOFMEMORY;
                            goto quit;
                        }
                        memcpy(new_objectinfo, objectinfo, sizeof(objectinfo[0]) * objectinfo_count);
                        objectinfo = new_objectinfo;
                        objectinfo_capacity = new_capacity;
                    }
                    wchar_t* id_copy = string_clone(object_id);
                    if (!id_copy) {
                        hr = E_OUTOFMEMORY;
                        goto quit;
                    }
                    wchar_t* name_copy = string_clone(current_object_name);
                    if (!name_copy) {
                        delete[] id_copy;
                        hr = E_OUTOFMEMORY;
                        goto quit;
                    }
                    objectinfo[objectinfo_count++] = { id_copy, name_copy, S_OK };
                }

                delete[] current_object_name;
                current_object_name = nullptr;
                CoTaskMemFree(object_id);
                object_ids[i] = nullptr;
            }
            nfetched = 0;
        }
    } while (hr == S_OK);

    quit:
    safe_release(&enumerator);
    delete[] current_object_name;
    if (FAILED(hr) && objectinfo) {
        for (int i = 0; i < objectinfo_count; ++i) {
            delete[] objectinfo[i].id;
            delete[] objectinfo[i].name;
        }
        delete[] objectinfo;
    }
    for (DWORD i = 0; i < nfetched; ++i) {
        CoTaskMemFree(object_ids[i]);
    }
    *out_objectinfo = SUCCEEDED(hr) ? objectinfo : nullptr;
    *out_nobjectinfo = SUCCEEDED(hr) ? objectinfo_count : 0;
    return hr;
}

static HRESULT find_device_object_by_path(IPortableDeviceContent* content, IPortableDeviceProperties* properties, const wchar_t* path, wchar_t** out_object_id) {
    wchar_t full_path[MAX_PATH];
    wchar_t curr_object_id[MAX_PATH];
    wchar_t* component = full_path;
    HRESULT hr;
    *out_object_id = nullptr;

    hr = PathCchCanonicalize(full_path, _countof(full_path), path);
    if (FAILED(hr)) goto quit;

    if (0 != wcscpy_s(curr_object_id, WPD_DEVICE_OBJECT_ID)) {
        hr = E_FAIL;
        goto quit;
    }

    do {
        auto backslash_pos = component ? wcschr(component, L'\\') : nullptr;
        int component_length = (int)(backslash_pos ? backslash_pos - component : wcslen(component));
        if (component_length == 0) {
            break;
        }
        wchar_t component_copy[MAX_PATH];
        if (0 != wcsncpy_s(component_copy, component, (size_t)component_length)) {
            hr = E_FAIL;
            goto quit;
        }

        wchar_t* next_object_id = nullptr;
        hr = find_device_object(content, properties, curr_object_id, component_copy, &next_object_id);
        if (FAILED(hr)) goto quit;

        if (next_object_id == nullptr) {
            hr = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
            goto quit;
        }

        bool copied = 0 == wcscpy_s(curr_object_id, next_object_id);
        delete[] next_object_id;

        if (!copied) {
            hr = E_FAIL;
            goto quit;
        }
    } while (component = PathFindNextComponentW(component));

    quit:
    if (SUCCEEDED(hr)) {
        auto copy = string_clone(curr_object_id);
        if (!copy) {
            hr = E_OUTOFMEMORY;
        }
        *out_object_id = copy;
    }
    return hr;
}

static void print_deviceinfo(PortableDeviceInformation* deviceinfo) {
    wprintf(L"- Identifier: \"%s\"\n", deviceinfo->id);
    wprintf(L"- Friendly Name: \"%s\"\n", deviceinfo->friendly_name ? deviceinfo->friendly_name : L"<not set>");
    wprintf(L"- Description: \"%s\"\n", deviceinfo->description ? deviceinfo->description : L"<not set>");
}

int wmain(int argc, wchar_t** argv) {
    if (argc == 1) {
        wprintf(
            L"Usage:\n"
            L"--device_friendly_name <string>   select device by it's friendly name\n"
            L"--device_description <string>     select device by it's description\n"
            L"--source_directory <path>         directory on device to copy files from\n"
            L"--destination_directory <path>    directory on PC to copy files to\n"
            L"--match <string>                  only files which contain this string will be copied\n"
            L"\n"
            L"--list_devices                    list all devices, other arguments are ignored\n"
            L"--copy_files                      copy matched files\n"
            L"--delete_files                    delete matched files\n"
            L"                                  If --copy_files is also set, deletes only copied files\n"
            L"--list_files                      show matched files\n"
        );
        return 0;
    }

    HRESULT hr = CoInitializeEx(0, COINIT_APARTMENTTHREADED | COINIT_SPEED_OVER_MEMORY | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr)) {
        wprintf(L"CoInitializeEx failed: %s\n", hresult_to_string(hr));
        return 1;
    }

    auto args = parse_args(argc, argv);
    if (!args.ok) {
        return 1;
    }

    // If copying files, normalize destination directory.
    if (args.copy_files) {
        wchar_t* new_destination_directory = nullptr;
        hr = PathAllocCanonicalize(args.destination_directory, PATHCCH_ALLOW_LONG_PATHS, &new_destination_directory);
        if (SUCCEEDED(hr)) {
            hr = PathCchRemoveBackslash(new_destination_directory, 1 + wcslen(new_destination_directory));
        }

        if (FAILED(hr)) {
            wprintf(L"Unable to normalize destination directory: %s\n", hresult_to_string(hr));
            return 1;
        }

        delete[] args.destination_directory;
        args.destination_directory = string_clone(new_destination_directory);
        LocalFree(new_destination_directory);
        if (!args.destination_directory) {
            hr = E_OUTOFMEMORY;
            wprintf(L"Unable to normalize destination directory: %s\n", hresult_to_string(hr));
            return 1;
        }
    }

    // Get all devices.
    int ndeviceinfos = 0;
    PortableDeviceInformation* deviceinfos = nullptr;
    PortableDeviceInformation* deviceinfo = nullptr; // <-- don't free.
    IPortableDeviceValues* client_information = nullptr;
    IPortableDevice* device = nullptr;
    IPortableDeviceContent* content = nullptr;
    IPortableDeviceProperties* properties = nullptr;
    IPortableDeviceResources* resources = nullptr;
    wchar_t* source_directory_object_id = nullptr;
    DeviceObjectInformation* src_objects = nullptr;
    int src_nobjects = 0;
    int copy_success_count = 0;
    IPortableDevicePropVariantCollection* files_to_delete = nullptr;
    IPortableDevicePropVariantCollection* file_deletion_results = nullptr;

    hr = enumerate_devices(&deviceinfos, &ndeviceinfos);
    if (FAILED(hr)) {
        wprintf(L"Unable to enumerate devices: %s\n", hresult_to_string(hr));
        goto quit;
    }

    if (ndeviceinfos == 0) {
        wprintf(L"No devices were found.\n");
        return 0;
    }

    // Show found devices.
    if (args.list_devices) {
        wprintf(L"Found %d devices:\n", ndeviceinfos);
        for (int i = 0; i < ndeviceinfos; ++i) {
            auto deviceinfo = &deviceinfos[i];
            wprintf(L"Device %d:\n", i);
            print_deviceinfo(deviceinfo);
        }
        return 0;
    }

    // Find matching device.
    deviceinfo = match_device(deviceinfos, ndeviceinfos, args);
    if (!deviceinfo) {
        wprintf(L"Unable to match device with provided arguments.\n");
        goto quit;
    }

    wprintf(L"Selected device:\n");
    print_deviceinfo(deviceinfo);

    // Client information.
    hr = CoCreateInstance(CLSID_PortableDeviceValues, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&client_information));
    if (FAILED(hr)) {
        wprintf(L"Unable to create client information structure: %s\n", hresult_to_string(hr));
        goto quit;
    }

    // Attempt to set all bits of client information.
    hr = client_information->SetStringValue(WPD_CLIENT_NAME, L"ru.miere.device_data_tool");
    if (SUCCEEDED(hr)) {
        hr = client_information->SetUnsignedIntegerValue(WPD_CLIENT_MAJOR_VERSION, 1);
        if (SUCCEEDED(hr)) {
            hr = client_information->SetUnsignedIntegerValue(WPD_CLIENT_MINOR_VERSION, 0);
            if (SUCCEEDED(hr)) {
                hr = client_information->SetUnsignedIntegerValue(WPD_CLIENT_REVISION, 0);
                if (SUCCEEDED(hr)) {
                    // Some device drivers need to impersonate the caller in order to function correctly.  Since our application does not
                    // need to restrict its identity, specify SECURITY_IMPERSONATION so that we work with all devices.
                    hr = client_information->SetUnsignedIntegerValue(WPD_CLIENT_SECURITY_QUALITY_OF_SERVICE, SECURITY_IMPERSONATION);
                }
            }
        }
    }

    if (FAILED(hr)) {
        wprintf(L"Unable to set client information: %s\n", hresult_to_string(hr));
        goto quit;
    }

    // Create device.
    hr = CoCreateInstance(CLSID_PortableDeviceFTM, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&device));
    if (FAILED(hr)) {
        wprintf(L"Unable to create device structure: %s\n", hresult_to_string(hr));
        goto quit;
    }

    // @TODO: Timeout
    hr = device->Open(deviceinfos[0].id, client_information);
    if (FAILED(hr)) {
        wprintf(L"Unable to connect to device: %s\n", hresult_to_string(hr));
        goto quit;
    }

    hr = device->Content(&content);
    if (SUCCEEDED(hr)) {
        hr = content->Transfer(&resources);
        if (SUCCEEDED(hr)) {
            hr = content->Properties(&properties);
        }
    }

    if (FAILED(hr)) {
        wprintf(L"Unable to get device structures: %s\n", hresult_to_string(hr));
        goto quit;
    }
   
    // Find source directory.
    hr = find_device_object_by_path(content, properties, args.source_directory, &source_directory_object_id);
    if (FAILED(hr)) {
        wprintf(L"Unable to get source directory on the device: %s\n", hresult_to_string(hr));
        goto quit;
    }

    // Get all source directory files (filtered).
    hr = enumerate_device_objects(content, properties, source_directory_object_id, &src_objects, &src_nobjects, (void*)args.match, [](const wchar_t* object_name, void* userdata) {
        const wchar_t* match = (const wchar_t*)userdata;
        return match ? wcsstr(object_name, (wchar_t*)userdata) != nullptr : true;
    });
    
    if (FAILED(hr)) {
        wprintf(L"Unable to enumerate device objects: %s\n", hresult_to_string(hr));
        goto quit;
    }

    if (src_nobjects == 0) {
        wprintf(L"No files were matched.\n");;
        hr = S_OK;
        goto quit;
    }

    // List files.
    if (args.list_files) {
        wprintf(L"Matched %d files:\n", src_nobjects);
        for (int i = 0; i < src_nobjects; ++i) {
            wprintf(L"- %s\n", src_objects[i].name);
        }
        hr = S_OK;
        goto quit;
    }

    // Copy files.
    if (args.copy_files) {
        wprintf(L"\nCopying %d files:\n", src_nobjects);
        for (int i = 0; i < src_nobjects; ++i) {
            DWORD optimal_buffer_size = 0;
            IStream* stream = nullptr;
            IStream* file_stream = nullptr;
            const wchar_t* error_context = nullptr;
            wchar_t* destination_path = nullptr;
            char* buffer = nullptr;

            hr = resources->GetStream(src_objects[i].id, WPD_RESOURCE_DEFAULT, STGM_READ, &optimal_buffer_size, &stream);
            if (FAILED(hr)) {
                error_context = L"Unable to get source file stream";
                goto copy_quit;
            }

            hr = PathAllocCombine(args.destination_directory, src_objects[i].name, PATHCCH_ALLOW_LONG_PATHS, &destination_path);
            if (FAILED(hr)) {
                error_context = L"Cannot build destination path";
                goto copy_quit;
            }

            hr = SHCreateStreamOnFileW(destination_path, STGM_CREATE | STGM_WRITE, &file_stream);
            if (FAILED(hr)) {
                error_context = L"Unable to create destination file";
                goto copy_quit;
            }

            buffer = new (std::nothrow) char[optimal_buffer_size];
            if (!buffer) {
                hr = E_OUTOFMEMORY;
                error_context = L"Unable to create copy buffer";
                goto copy_quit;
            }

            while (1) {
                DWORD nread = 0;
                DWORD nwritten = 0;

                hr = stream->Read(buffer, optimal_buffer_size, &nread);
                if (FAILED(hr)) {
                    error_context = L"Unable to read from source file";
                    goto copy_quit;
                }

                if (nread == 0) {
                    break;
                }

                hr = file_stream->Write(buffer, nread, &nwritten);
                if (FAILED(hr)) {
                    error_context = L"Unable to write to destination file";
                    goto copy_quit;
                }

                if (nwritten != nread) {
                    hr = E_FAIL;
                    error_context = L"Incomplete write to destination file";
                    goto copy_quit;
                }
            }

            copy_quit:
            LocalFree(destination_path);
            delete[] buffer;
            safe_release(&stream);
            safe_release(&file_stream);
            if (SUCCEEDED(hr)) {
                wprintf(L"- [OK] %s\n", src_objects[i].name);
                src_objects[i].hr = S_OK;
                ++copy_success_count;
            } else {
                wprintf(L"- [FAILED] %s\n  - %s: %s\n", src_objects[i].name, error_context, hresult_to_string(hr));
                src_objects[i].hr = hr;
            }
        }
    }

    // Delete files.
    if (args.delete_files) {
        wprintf(L"\nDeleting %d files:\n", args.copy_files ? copy_success_count : src_nobjects);

        hr = CoCreateInstance(CLSID_PortableDevicePropVariantCollection, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&files_to_delete));
        if (FAILED(hr)) {
            wprintf(L"Cannot create collection to hold deletion files: %s\n", hresult_to_string(hr));
            goto quit;
        }

        int delete_count = 0;
        for (int i = 0; i < src_nobjects; ++i) {
            if (FAILED(src_objects[i].hr)) {
                continue;
            }

            PROPVARIANT file;
            PropVariantInit(&file);

            size_t id_size = (1 + wcslen(src_objects[i].id)) * sizeof(wchar_t);
            file.vt = VT_LPWSTR;
            file.pwszVal = (wchar_t*)CoTaskMemAlloc(id_size);
            if (!file.pwszVal) {
                src_objects[i].hr = E_OUTOFMEMORY;
                continue;
            }
            memcpy(file.pwszVal, src_objects[i].id, id_size);

            hr = files_to_delete->Add(&file);
            if (FAILED(hr)) {
                src_objects[i].hr = hr;
                continue;
            }
            ++delete_count;
        }

        hr = content->Delete(PORTABLE_DEVICE_DELETE_NO_RECURSION, files_to_delete, &file_deletion_results);
        if (FAILED(hr)) {
            wprintf(L"Unable to delete files: %s\n", hresult_to_string(hr));
            goto quit;
        }

        for (int i = 0, j = 0; i < src_nobjects; ++i) {
            const wchar_t* error_context = nullptr;
            bool is_valid = false;
            PROPVARIANT value;

            if (FAILED(src_objects[i].hr)) {
                hr = src_objects[i].hr;
                goto delete_quit;
            }
            is_valid = true;

            hr = file_deletion_results->GetAt(j, &value);
            if (FAILED(hr)) {
                error_context = L"Unable to get file deletion result";
                goto delete_quit;
            }

            if (value.vt != VT_ERROR) {
                hr = E_FAIL;
                error_context = L"Unexpected file deleting result value type";
                goto delete_quit;
            }

            hr = HRESULT_FROM_WIN32(value.scode);
            if (FAILED(hr)) {
                error_context = L"Deletion error";
                goto delete_quit;
            }

            delete_quit:
            if (!is_valid) {
                continue;
            }

            ++j;
            if (SUCCEEDED(hr)) {
                wprintf(L"- [OK] %s\n", src_objects[i].name);
            } else {
                wprintf(L"- [FAILED] %s\n  - %s: %s\n", src_objects[i].name, error_context, hresult_to_string(hr));
            }

            PropVariantClear(&value);
        }
    }

    hr = S_OK;

    quit:
    delete[] deviceinfos;
    safe_release(&client_information);
    safe_release(&content);
    safe_release(&resources);
    safe_release(&properties);
    safe_release(&files_to_delete);
    safe_release(&file_deletion_results);
    delete[] source_directory_object_id;
    for (int i = 0; i < src_nobjects; ++i) {
        delete[] src_objects[i].id;
        delete[] src_objects[i].name;
    }
    delete[] src_objects;

    if (device) {
        // Close explicitly to avoid Windows Explorer hanging after deleting files via this program.
        HRESULT close_hr = device->Close();
        if (FAILED(close_hr)) {
            wprintf(L"Unable to close device: %s\n", hresult_to_string(close_hr));
        }

        ULONG last_reference = device->Release();
        assert(last_reference == 0);
        device = nullptr;
    }

    CoUninitialize();
    return SUCCEEDED(hr) ? 0 : 1;
}