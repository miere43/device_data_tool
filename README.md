Command line program to list connected devices, copy and delete files from devices using Windows Portable Devices (WPD) API.

## Usage
```
--device_friendly_name <string>   select device by it's friendly name
--device_description <string>     select device by it's description
--source_directory <path>         directory on device to copy files from
--destination_directory <path>    directory on PC to copy files to
--match <string>                  only files which contain this string will be copied

--list_devices                    list all devices, other arguments are ignored
--copy_files                      copy matched files
--delete_files                    delete matched files
                                  If --copy_files is also set, deletes only copied files
--list_files                      show matched files
```

Example: copy files which file name contain string "IMG_" from device with description (name) "Camera1" from device's folder "Internal shared storage\DCIM\Camera" into PC's folder "D:\Photos", then delete copied files from the device.
```
device_data_tool.exe --device_description "Camera1" --source_directory "Internal shared storage\DCIM\Camera" --destination_directory "D:\Photos" --match ".png" --copy_files --delete_files
```

If you don't know your device's name, run application with switch `--list_devices` to show information about all connected devices.

## Requirements
* Windows 8, Windows 8.1 or Windows 10 (tested on Windows 10);
* Microsoft Visual C++ 2017 Redistributable.

## License
Public Domain.