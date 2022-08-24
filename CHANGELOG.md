104.1
-----

* Samples
    * Added thorough Python USD transform handling with the xform_utils.py module
    * Added a better logging system in the Python hello world example
    * Formatted the help text in OmniCLI to be spaced properly and to include arguments
    * Added omniSimpleSensor, a simple example of simulating sensor data pushed into a USD
    * Added omniSensorThread, this is the separate working thread to change a USD
    * Added physics to HelloWorld sample to showcase UsdPhysics usage.
    * Added the file hash to the stat output in OmniCLI
    * Added "cver" command to print client version
* Omniverse Client Library
    * 1.17.4
    * OM-47554: Downgrade some "Error" messages during auth to "Verbose"
        * We attempt to connect using 3 different methods, and it's normal for 2 of them to fail. This avoids confusing users with error messages for a connection that ultimately succeeds.
        * OM-48252: Lower required "list2" version, to allow connecting to servers running Nucleus 112.0.
    * 1.17.3
        * CC-357: Fixed a deadlock that could occur when a python file status callback is being unregistered on one thread while another thread is simultaneously trying to call that file status callback
        * CC-367: Allow stat & list using cloudfront.net URLs
        * OM-45178: Print extended error message when Token.subscribe fails
        * OM-45887: Enable extra connection logging
        * CC-384: Remove support for Nucleus 107, 109, and 110
        * CC-366: Update OpenSSL to avoid a security vulnerability
    * 1.17.2
        * CC-32: Fixed a crash that could happen on disconnect
        * OM-43009: Removed "stop" from list of required capabilities, to discover newer servers which don't advertise that capability
        * CC-236: Add support for checkpoint change notification
        * CC-245: Fixed a deadlock that could occur rarely when connection failed.
    * 1.17.1
        * CC-228: subscription based authentication using 'nonce'
        * CC-231: Discover API server using minimum required capabilities rather than all capabilites
        * CC-229: Fixed a case where the client library would not connect to Nucleus securely
    * 1.17.0
        * CC-7: Add shutdown guard to omniclient::Core


103.2
-----
* Samples
    * Change the live rotations from ZYX to XYZ and all xform ops to double3
* Omniverse Client Library
    * 1.16.0
        * OM-39826: Prevent copying of channels from nucleus servers to other providers
        * OM-38687: Fix crash when shutdown with an outstanding stat subscription
        * OM-37095: Use omniClientWait in blocking python binding functions
        * OM-38761: Fix ResolveSubscribe to handle more invalid search paths
        * OM-39746: Support stat('/') on S3 buckets. S3 API does not support stat() on root, but we can fill in the gaps
    * 1.15.0
        * OM-39614: Fixed a case where pings would not report a connection error
        * OM-36524: update connection library to prevent using Nucleus Cache when accessing localhost servers
        * OM-37061: Fix crash if a request is started after it is stopped
        * OM-34916: Map MountExistsUnderPath error to ErrorNotSupported
        * OM-38761: Fixed StatImpl::beginWatch to always call the provided callback, even when the uri to watch is invalid.
        * OM-39367: Removed "fast locked updates" because it's fundamentally broken.
            * Removes omniUsdLiveLock & omniUsdLiveUnlock
        * OM-23042: If the relativePath starts with "./" then don't use search paths
    * 1.14.0
        * OM-38721: Added function omniClientGetLocalFile which returns the local filename for a URL
            * If the input URL is already a local file, it is returned without error
            * If the remote file has not yet been downloaded to cache, it is downloaded before returning
            * Python bindings are "get_local_file" "get_local_file_async" and "get_local_file_with_callback"
        * OM-38816: Added environment variable OMNI_DEPLOYMENT which can override the deployment sent to discovery
        * OM-37061: Early exit from callback if cacheEntry weak_ptr is expired, correct creation of cacheEntry ownership
    * 1.13.25
        * OM-34145: Fix omniClientCopy to not infinitely copy when copying a directory into a subdirectory of itself.
    * 1.13.24
        * OM-38028: Update Brotli, OpenSSL, and libcurl versions
    * 1.13.23
        * OM-37701: Fix FetchToLocalResolvedPath to work with SdfFileFormat arguments
    * 1.13.22
        * OM-37276: Use latest idl.cpp to pickup SSL cert directory location fixes
    * 1.13.21
        * OM-36064 & OM-36306: Fix crash in listSubscribe on disconnect
    * 1.13.20
        * OM-37054: Fix incorrect search order according to PBR specification
        * OM-36511: Add python bindings set_authentication_message_box_callback & authentication_cancel


103.1
-----
* Samples
    * Added omniUsdReader, a very very simple program for build config demonstration that opens a stage and traverses it, printing all of the prims
    * Added omniUsdaWatcher, a live USD watcher that outputs a constantly updating USDA file on disk
    * Updated the nv-usd library to one with symbols so the Visual Studio Debug Visualizers work properly
* Omniverse Client Library
    * Still using 1.13.19

102.1
-----
* Samples
    * OM-31648: Add a windows build tool configuration utility if the user wants to use an installed MSVC and the Windows SDK
    * Add a dome light with texture to the stage
    * OM-35991: Modify the MDL names and paths to reduce some code redundancy based on a forum post
    * Add Nucleus checkpoints to the Python sample
    * Avoid writing Nucleus checkpoints when live mode is enabled, this isn't supported properly
    * OM-37005: Fix a bug in the Python sample batch file if the sample was installed in a path with spaces
    * Make the /Root prim the `defaultPrim`
    * Update Omniverse Client Library to 1.13.19
* Omniverse Client Library
    * 1.13.19
        * OM-36925: Fix omniClientMakeRelative("omni://host/path/", "omni://host/path/");
    * 1.13.18
        * OM-25931: Fixed some issues around changing and calling the log callback to reduce hangs.
        * OM-36755: Fixed possible use-after-delete issue with set_log_callback (Python).
    * 1.13.17
        * OM-34879: Hard-code "mdl" as "not a layer" to work around a problem that happens if the "usdMdl" plugin is loaded
    * 1.13.16
        * OM-36756: Fix crash that could happen if two threads read a layer at the exact same time.
    * 1.13.15
        * OM-35235: Fix various hangs by changing all bindings to release the GIL except in very specific cases.
    * 1.13.14
        * OM-34879: Fix hang in some circumstances by delaying USD plugin registration until later
        * OM-33732: Remove USD diagnostic delegate
    * 1.13.13
        * OM-36256: Fixed S3 provider from generating a bad AWS signature when Omni Cache is enabled
    * 1.13.12
        * OM-20572: Fixed setAcls
    * 1.13.11
        * OM-35397: Fixed a bug that caused Linux's File Watcher Thread to peg the CPU in some cases.
    * 1.13.10
        * OM-32244: Fixed a very rare crash that could occur when reading a local file that another process has locked
    * 1.13.9
        * OM-35050: Fixed problem reloading a non-live layer after it's been modified.
    * 1.13.8
        * OM-34739: Fix regression loading MDLs introduced in     * 1.13.3
        * OM-33949: makeRelativeUrl prepends "./" to relative paths
        * OM-34752: Make sure local paths are always using "" inside USD on Windows
    * 1.13.7
        * OM-34696: Fixed bug when S3 + cloudfront + omni cache are all used
    * 1.13.6
        * OM-33914: Fixed crash when accessing http provider from mulitple threads simultaneously
    * 1.13.5
        * OM-26039: Fixed "Restoring checkpoint while USD stage is opened live wipes the content"
        * OM-33753: Fixed "running massive amounts of live edits together causes massive amounts of checkpoints"
        * OM-34432: Fixed "[Create] It will lose all data or hang Create in live session"
            * These were all the same underlying issue: When a layer is overwritten in live mode it was cleared and set as 'dirty' which would cause the next "Save()" (which happens every frame in live mode) to save the cleared layer back to the Omniverse server.
    * 1.13.4
        * OM-31830: omniClientCopy() with HTTP/S3 provider as source
        * OM-33321: Use Omni Cache 2.4.1+ new reverse proxy feature for HTTPS caching
    * 1.13.3
        * OM-33483: Don't crash when trying to save a layer that came from a mount
        * OM-27233: Support loading non-USD files (abc, drc, etc)
        * OM-4613 & OM-34150: Support saving usda files as ascii
            * Note this change means live updates no longer work with usda files (though they technically never did -- it would silently convert them to usdc files).

101.1
-----
* Add Linux package for the Omniverse Launcher
* Add a python 3 Hello World sample
* Update the Omniverse Client Library to 1.13.2
* Update to Python 3.7
* Add a Nucleus Checkpoint example
* Add the ability to create/access a USD stage on local disk in the Hello World sample

100.2
-----
* Update the Omniverse Client Library fix an issue with overlapping file writes

100.1
-----
* First release
* HelloWorld sample that demonstrates how to:
    * connect to an Omniverse server
    * create a USD stage
    * create a polygonal box and add it to the stage
    * upload an MDL material and its textures to an Omniverse server
    * bind an MDL and USD Preview Surface material to the box
    * add a light to the stage
    * move and rotate the box with live updates
    * print verbose Omniverse logs
    * open an existing stage and find a mesh to do live edits
* OmniCLI sample that exercises most of the Omniverse Client Library API
