#include "sensor_manager.h"
#include "sensor_model.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <comdef.h>
    #include <wbemidl.h>
    #pragma comment(lib, "wbemuuid.lib")
    #pragma comment(lib, "ole32.lib")
    #pragma comment(lib, "oleaut32.lib")
#elif defined(__linux__)
    #include <dirent.h>
    #include <fcntl.h>
    #include <unistd.h>
    #include <fstream>
    #include <sstream>
#elif defined(__APPLE__)
    #include <sys/types.h>
    #include <sys/sysctl.h>
    #include <CoreFoundation/CoreFoundation.h>
    #include <IOKit/IOKitLib.h>
    #include <dlfcn.h>
#endif

// NVML function pointer types (dynamically loaded)
#if !defined(__APPLE__)
typedef int (*nvmlInit_t)();
typedef int (*nvmlShutdown_t)();
typedef int (*nvmlDeviceGetCount_t)(unsigned int*);
typedef int (*nvmlDeviceGetHandleByIndex_t)(unsigned int, void**);
typedef int (*nvmlDeviceGetTemperature_t)(void*, int, unsigned int*);
typedef int (*nvmlDeviceGetPowerUsage_t)(void*, unsigned int*);
typedef int (*nvmlDeviceGetName_t)(void*, char*, unsigned int);
#endif

namespace occt {

// ─── Constructor / Destructor ────────────────────────────────────────────────

SensorManager::SensorManager() = default;

SensorManager::~SensorManager() {
    stop();

    // Unload NVML
    if (nvml_handle_) {
#if defined(_WIN32)
        using ShutdownFn = int (*)();
        auto fn = reinterpret_cast<ShutdownFn>(
            GetProcAddress(static_cast<HMODULE>(nvml_handle_), "nvmlShutdown"));
        if (fn) fn();
        FreeLibrary(static_cast<HMODULE>(nvml_handle_));
#elif defined(__linux__)
        using ShutdownFn = int (*)();
        auto fn = reinterpret_cast<ShutdownFn>(dlsym(nvml_handle_, "nvmlShutdown"));
        if (fn) fn();
        dlclose(nvml_handle_);
#endif
        nvml_handle_ = nullptr;
    }

    // Unload ADL
    if (adl_handle_) {
#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(adl_handle_));
#elif defined(__linux__)
        dlclose(adl_handle_);
#endif
        adl_handle_ = nullptr;
    }
}

// ─── Public API ──────────────────────────────────────────────────────────────

bool SensorManager::initialize() {
    bool any = false;

#if defined(_WIN32)
    has_wmi_ = init_wmi();
    any |= has_wmi_;
#elif defined(__linux__)
    has_sysfs_ = init_sysfs();
    any |= has_sysfs_;
#elif defined(__APPLE__)
    has_iokit_ = init_iokit();
    any |= has_iokit_;
#endif

    // GPU backends (cross-platform where applicable)
    has_nvml_ = init_nvml();
    any |= has_nvml_;

    has_adl_ = init_adl();
    any |= has_adl_;

    return any;
}

void SensorManager::start_polling(int interval_ms) {
    if (running_.load()) return;
    running_.store(true);
    poll_thread_ = std::thread(&SensorManager::poll_thread_func, this, interval_ms);
}

void SensorManager::stop() {
    running_.store(false);
    if (poll_thread_.joinable()) {
        poll_thread_.join();
    }
}

std::vector<SensorReading> SensorManager::get_all_readings() const {
    std::lock_guard<std::mutex> lk(readings_mutex_);
    return readings_;
}

QVector<HardwareNode> SensorManager::get_hardware_tree() const {
    std::lock_guard<std::mutex> lk(readings_mutex_);
    QVector<SensorReading> qreadings;
    qreadings.reserve(static_cast<int>(readings_.size()));
    for (const auto& r : readings_) {
        qreadings.append(r);
    }
    return build_hardware_tree(qreadings);
}

double SensorManager::get_cpu_temperature() const {
    std::lock_guard<std::mutex> lk(readings_mutex_);
    for (const auto& r : readings_) {
        if (r.category == "CPU" && r.unit == "C") {
            return r.value;
        }
    }
    return 0.0;
}

double SensorManager::get_gpu_temperature() const {
    std::lock_guard<std::mutex> lk(readings_mutex_);
    for (const auto& r : readings_) {
        if (r.category == "GPU" && r.unit == "C") {
            return r.value;
        }
    }
    return 0.0;
}

double SensorManager::get_cpu_power() const {
    std::lock_guard<std::mutex> lk(readings_mutex_);
    for (const auto& r : readings_) {
        if (r.category == "CPU" && r.unit == "W") {
            return r.value;
        }
    }
    return 0.0;
}

void SensorManager::set_alert_callback(AlertCallback cb) {
    std::lock_guard<std::mutex> lk(cb_mutex_);
    alert_cb_ = std::move(cb);
}

// ─── Polling Thread ──────────────────────────────────────────────────────────

void SensorManager::poll_thread_func(int interval_ms) {
    while (running_.load()) {
#if defined(_WIN32)
        if (has_wmi_) poll_wmi();
#elif defined(__linux__)
        if (has_sysfs_) poll_sysfs();
#elif defined(__APPLE__)
        if (has_iokit_) poll_iokit();
#endif

        if (has_nvml_) poll_nvml();
        if (has_adl_) poll_adl();

        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    }
}

// ─── Helper: update or insert a reading ──────────────────────────────────────

void SensorManager::update_reading(const std::string& sensor_name,
                                   const std::string& category,
                                   double value, const std::string& unit) {
    std::lock_guard<std::mutex> lk(readings_mutex_);

    for (auto& r : readings_) {
        if (r.name == sensor_name && r.category == category) {
            r.value = value;
            r.min_value = std::min(r.min_value, value);
            r.max_value = std::max(r.max_value, value);
            return;
        }
    }

    // New sensor
    SensorReading reading;
    reading.name = sensor_name;
    reading.category = category;
    reading.value = value;
    reading.min_value = value;
    reading.max_value = value;
    reading.unit = unit;
    readings_.push_back(std::move(reading));
}

// ─── Linux: sysfs backend ────────────────────────────────────────────────────

#if defined(__linux__)

bool SensorManager::init_sysfs() {
    // Check /sys/class/thermal exists
    DIR* dir = opendir("/sys/class/thermal");
    if (dir) {
        closedir(dir);
        return true;
    }
    // Check /sys/class/hwmon
    dir = opendir("/sys/class/hwmon");
    if (dir) {
        closedir(dir);
        return true;
    }
    return false;
}

void SensorManager::poll_sysfs() {
    // Read thermal zones
    for (int i = 0; i < 16; ++i) {
        std::string path = "/sys/class/thermal/thermal_zone" +
                           std::to_string(i) + "/temp";
        std::ifstream f(path);
        if (!f.is_open()) break;

        int temp_milli = 0;
        f >> temp_milli;
        double temp_c = temp_milli / 1000.0;

        // Read zone type
        std::string type_path = "/sys/class/thermal/thermal_zone" +
                                std::to_string(i) + "/type";
        std::ifstream tf(type_path);
        std::string zone_type = "zone" + std::to_string(i);
        if (tf.is_open()) {
            std::getline(tf, zone_type);
        }

        std::string category = "CPU";
        if (zone_type.find("gpu") != std::string::npos ||
            zone_type.find("GPU") != std::string::npos) {
            category = "GPU";
        }

        update_reading(zone_type, category, temp_c, "C");
    }

    // Read hwmon sensors (coretemp, etc.)
    DIR* dir = opendir("/sys/class/hwmon");
    if (!dir) return;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.') continue;

        std::string hwmon_path = std::string("/sys/class/hwmon/") + entry->d_name;

        // Read sensor name
        std::string name_path = hwmon_path + "/name";
        std::ifstream nf(name_path);
        std::string sensor_name = entry->d_name;
        if (nf.is_open()) std::getline(nf, sensor_name);

        // Read temp inputs
        for (int j = 1; j <= 32; ++j) {
            std::string temp_path = hwmon_path + "/temp" + std::to_string(j) + "_input";
            std::ifstream tf(temp_path);
            if (!tf.is_open()) break;

            int temp_milli = 0;
            tf >> temp_milli;
            double temp_c = temp_milli / 1000.0;

            std::string label = sensor_name + "_temp" + std::to_string(j);

            // Try reading label
            std::string label_path = hwmon_path + "/temp" + std::to_string(j) + "_label";
            std::ifstream lf(label_path);
            if (lf.is_open()) {
                std::getline(lf, label);
            }

            std::string category = "Motherboard";
            if (sensor_name == "coretemp" || sensor_name == "k10temp" ||
                sensor_name == "zenpower") {
                category = "CPU";
            } else if (sensor_name.find("gpu") != std::string::npos ||
                       sensor_name == "amdgpu" || sensor_name == "nouveau") {
                category = "GPU";
            }

            update_reading(label, category, temp_c, "C");
        }

        // Read power (in1_input is voltage in mV, power1_input in uW)
        std::string power_path = hwmon_path + "/power1_input";
        std::ifstream pf(power_path);
        if (pf.is_open()) {
            uint64_t power_uw = 0;
            pf >> power_uw;
            double watts = power_uw / 1e6;
            update_reading(sensor_name + "_power", "CPU", watts, "W");
        }

        // Read fan RPM
        for (int j = 1; j <= 8; ++j) {
            std::string fan_path = hwmon_path + "/fan" + std::to_string(j) + "_input";
            std::ifstream ff(fan_path);
            if (!ff.is_open()) break;

            int rpm = 0;
            ff >> rpm;
            update_reading(sensor_name + "_fan" + std::to_string(j),
                           "Motherboard", static_cast<double>(rpm), "RPM");
        }
    }

    closedir(dir);
}

#else // !__linux__
bool SensorManager::init_sysfs() { return false; }
void SensorManager::poll_sysfs() {}
#endif

// ─── macOS: IOKit SMC backend ────────────────────────────────────────────────

#if defined(__APPLE__)

// SMC key types (Apple Silicon + Intel Mac)
// We read from IOKit thermal sensors

bool SensorManager::init_iokit() {
    // Check if IOKit thermal sensors are available
    io_iterator_t iter = 0;
    kern_return_t kr = IOServiceGetMatchingServices(
        kIOMainPortDefault,
        IOServiceMatching("AppleSMC"),
        &iter);

    if (kr == KERN_SUCCESS && iter != 0) {
        IOObjectRelease(iter);
        return true;
    }

    // Also check AppleARMIODevice for Apple Silicon
    kr = IOServiceGetMatchingServices(
        kIOMainPortDefault,
        IOServiceMatching("IOHIDSensor"),
        &iter);

    if (kr == KERN_SUCCESS && iter != 0) {
        IOObjectRelease(iter);
        return true;
    }

    // Fallback: try reading thermal pressure via sysctl
    return true; // macOS always has some sensor info
}

void SensorManager::poll_iokit() {
    // Use sysctl for CPU die temperature on Apple Silicon
    // This is a simplified approach; full SMC access requires
    // a more complex IOKit implementation

    // macOS thermal pressure (available on all modern macOS)
    // "machdep.xcpm.cpu_thermal_level" for Intel
    // For Apple Silicon, use IOReport framework

    // Read CPU thermal level via sysctl (approximate)
    int thermal_level = 0;
    size_t len = sizeof(thermal_level);

    // Try Intel thermal sensor
    if (sysctlbyname("machdep.xcpm.cpu_thermal_level", &thermal_level,
                     &len, nullptr, 0) == 0) {
        // Thermal level is 0-100 (not degrees, but indicates thermal state)
        update_reading("CPU Thermal Level", "CPU",
                       static_cast<double>(thermal_level), "%");
    }

    // For actual temperature on macOS, we'd need to use IOKit SMC
    // or a library like osx-cpu-temp. The SMC protocol is undocumented
    // but reverse-engineered. For a production build we'd integrate
    // direct SMC key reads (TC0P for CPU, TG0P for GPU).

    // Placeholder: IOKit HID sensors for ambient/die temperature
    io_iterator_t iter = 0;
    kern_return_t kr = IOServiceGetMatchingServices(
        kIOMainPortDefault,
        IOServiceMatching("IOHIDSensor"),
        &iter);

    if (kr == KERN_SUCCESS) {
        io_object_t sensor;
        while ((sensor = IOIteratorNext(iter)) != 0) {
            CFTypeRef product = IORegistryEntryCreateCFProperty(
                sensor, CFSTR("Product"), kCFAllocatorDefault, 0);

            CFTypeRef primary = IORegistryEntryCreateCFProperty(
                sensor, CFSTR("PrimaryUsagePage"), kCFAllocatorDefault, 0);

            if (product && primary) {
                // Usage page 0xFF00 + usage 0x0005 = temperature
                int32_t usage_page = 0;
                if (CFGetTypeID(primary) == CFNumberGetTypeID()) {
                    CFNumberGetValue(static_cast<CFNumberRef>(primary),
                                     kCFNumberSInt32Type, &usage_page);
                }

                if (usage_page == 0xFF00) {
                    // This is a temperature sensor
                    CFTypeRef current_val = IORegistryEntryCreateCFProperty(
                        sensor, CFSTR("CurrentValue"), kCFAllocatorDefault, 0);

                    if (current_val && CFGetTypeID(current_val) == CFNumberGetTypeID()) {
                        int64_t raw_val = 0;
                        CFNumberGetValue(static_cast<CFNumberRef>(current_val),
                                         kCFNumberSInt64Type, &raw_val);

                        // Convert fixed-point to double (varies by sensor)
                        double temp = raw_val / 65536.0;

                        char name_buf[128] = "Unknown";
                        if (CFGetTypeID(product) == CFStringGetTypeID()) {
                            CFStringGetCString(static_cast<CFStringRef>(product),
                                               name_buf, sizeof(name_buf),
                                               kCFStringEncodingUTF8);
                        }

                        std::string sname(name_buf);
                        std::string cat = "Motherboard";
                        if (sname.find("CPU") != std::string::npos ||
                            sname.find("Die") != std::string::npos) {
                            cat = "CPU";
                        } else if (sname.find("GPU") != std::string::npos) {
                            cat = "GPU";
                        }

                        update_reading(sname, cat, temp, "C");
                        CFRelease(current_val);
                    }
                }
            }

            if (product) CFRelease(product);
            if (primary) CFRelease(primary);
            IOObjectRelease(sensor);
        }
        IOObjectRelease(iter);
    }
}

#else // !__APPLE__
bool SensorManager::init_iokit() { return false; }
void SensorManager::poll_iokit() {}
#endif

// ─── Windows: WMI backend ────────────────────────────────────────────────────

#if defined(_WIN32)

bool SensorManager::init_wmi() {
    HRESULT hr;

    try {
        hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    } catch (...) {
        std::cerr << "[Sensor] Warning: CoInitializeEx threw exception, trying fallback" << std::endl;
        hr = E_FAIL;
    }

    if (hr == RPC_E_CHANGED_MODE) {
        // COM already initialized with a different threading model -
        // this is fine, we can still use it.
        std::cerr << "[Sensor] COM already initialized (apartment-threaded), continuing" << std::endl;
    } else if (FAILED(hr)) {
        std::cerr << "[Sensor] Warning: COM initialization failed (0x"
                  << std::hex << hr << std::dec << "), falling back to basic system info" << std::endl;
        collect_basic_system_info();
        return false;
    }

    hr = CoInitializeSecurity(
        nullptr, -1, nullptr, nullptr,
        RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        nullptr, EOAC_NONE, nullptr);

    // RPC_E_TOO_LATE is OK if already initialized
    if (FAILED(hr) && hr != RPC_E_TOO_LATE) {
        std::cerr << "[Sensor] Warning: CoInitializeSecurity failed (0x"
                  << std::hex << hr << std::dec << "), WMI may have limited access" << std::endl;
        // Don't return false - WMI might still work without explicit security setup
    }

    return true;
}

void SensorManager::collect_basic_system_info() {
    // Fallback when WMI is not available: use basic Win32 API
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    update_reading("CPU Cores", "CPU",
                   static_cast<double>(si.dwNumberOfProcessors), "count");

    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) {
        update_reading("Memory Load", "System",
                       static_cast<double>(mem.dwMemoryLoad), "%");
        update_reading("Total Physical", "System",
                       static_cast<double>(mem.ullTotalPhys) / (1024.0 * 1024.0), "MB");
        update_reading("Available Physical", "System",
                       static_cast<double>(mem.ullAvailPhys) / (1024.0 * 1024.0), "MB");
    }
}

void SensorManager::poll_wmi() {
    // WMI query timeout: 5 seconds (prevents hangs on some machines)
    static const long kWmiTimeoutMs = 5000;

    IWbemLocator* locator = nullptr;
    IWbemServices* services = nullptr;

    HRESULT hr = CoCreateInstance(
        CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
        IID_IWbemLocator, reinterpret_cast<void**>(&locator));
    if (FAILED(hr)) {
        // WMI not accessible - use basic fallback
        collect_basic_system_info();
        return;
    }

    hr = locator->ConnectServer(
        _bstr_t(L"ROOT\\WMI"), nullptr, nullptr, nullptr,
        0, nullptr, nullptr, &services);

    if (hr == WBEM_E_ACCESS_DENIED) {
        std::cerr << "[Sensor] Warning: WMI access denied (no admin privileges), "
                  << "using basic system info" << std::endl;
        collect_basic_system_info();
        locator->Release();
        return;
    }

    if (SUCCEEDED(hr)) {
        // Query MSAcpi_ThermalZoneTemperature
        IEnumWbemClassObject* enumerator = nullptr;
        hr = services->ExecQuery(
            _bstr_t(L"WQL"),
            _bstr_t(L"SELECT * FROM MSAcpi_ThermalZoneTemperature"),
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
            nullptr, &enumerator);

        if (SUCCEEDED(hr) && enumerator) {
            IWbemClassObject* obj = nullptr;
            ULONG returned = 0;
            int zone_idx = 0;

            // Use timeout instead of WBEM_INFINITE to prevent hanging
            while (enumerator->Next(kWmiTimeoutMs, 1, &obj, &returned) == S_OK) {
                VARIANT vt;
                hr = obj->Get(L"CurrentTemperature", 0, &vt, nullptr, nullptr);
                if (SUCCEEDED(hr)) {
                    // WMI reports in tenths of Kelvin
                    double temp_c = (vt.intVal / 10.0) - 273.15;
                    update_reading("ACPI Zone " + std::to_string(zone_idx),
                                   "CPU", temp_c, "C");
                    VariantClear(&vt);
                }
                obj->Release();
                zone_idx++;
            }
            enumerator->Release();
        } else if (hr == WBEM_E_ACCESS_DENIED) {
            std::cerr << "[Sensor] Warning: WMI thermal query access denied" << std::endl;
        }
        services->Release();
    }

    // Also try ROOT\CIMV2 for fans
    hr = locator->ConnectServer(
        _bstr_t(L"ROOT\\CIMV2"), nullptr, nullptr, nullptr,
        0, nullptr, nullptr, &services);

    if (SUCCEEDED(hr)) {
        IEnumWbemClassObject* enumerator = nullptr;
        hr = services->ExecQuery(
            _bstr_t(L"WQL"),
            _bstr_t(L"SELECT * FROM Win32_Fan"),
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
            nullptr, &enumerator);

        if (SUCCEEDED(hr) && enumerator) {
            IWbemClassObject* obj = nullptr;
            ULONG returned = 0;
            int fan_idx = 0;

            while (enumerator->Next(kWmiTimeoutMs, 1, &obj, &returned) == S_OK) {
                VARIANT vt;
                hr = obj->Get(L"DesiredSpeed", 0, &vt, nullptr, nullptr);
                if (SUCCEEDED(hr)) {
                    update_reading("Fan " + std::to_string(fan_idx),
                                   "Motherboard",
                                   static_cast<double>(vt.intVal), "RPM");
                    VariantClear(&vt);
                }
                obj->Release();
                fan_idx++;
            }
            enumerator->Release();
        }
        services->Release();
    }

    locator->Release();
}

#else // !_WIN32
bool SensorManager::init_wmi() { return false; }
void SensorManager::poll_wmi() {}
void SensorManager::collect_basic_system_info() {}
#endif

// ─── NVIDIA NVML (dynamic loading) ──────────────────────────────────────────

bool SensorManager::init_nvml() {
#if defined(_WIN32)
    nvml_handle_ = LoadLibraryA("nvml.dll");
    if (!nvml_handle_) return false;

    auto init_fn = reinterpret_cast<nvmlInit_t>(
        GetProcAddress(static_cast<HMODULE>(nvml_handle_), "nvmlInit_v2"));
    if (!init_fn) {
        FreeLibrary(static_cast<HMODULE>(nvml_handle_));
        nvml_handle_ = nullptr;
        return false;
    }
    return init_fn() == 0; // NVML_SUCCESS = 0

#elif defined(__linux__)
    nvml_handle_ = dlopen("libnvidia-ml.so.1", RTLD_NOW);
    if (!nvml_handle_) {
        nvml_handle_ = dlopen("libnvidia-ml.so", RTLD_NOW);
    }
    if (!nvml_handle_) return false;

    auto init_fn = reinterpret_cast<nvmlInit_t>(dlsym(nvml_handle_, "nvmlInit_v2"));
    if (!init_fn) {
        dlclose(nvml_handle_);
        nvml_handle_ = nullptr;
        return false;
    }
    return init_fn() == 0;

#else
    // macOS: NVIDIA drivers not available on modern macOS
    return false;
#endif
}

void SensorManager::poll_nvml() {
    if (!nvml_handle_) return;

#if defined(_WIN32)
    #define LOAD_NVML(name) reinterpret_cast<name##_t>( \
        GetProcAddress(static_cast<HMODULE>(nvml_handle_), #name))
#elif defined(__linux__)
    #define LOAD_NVML(name) reinterpret_cast<name##_t>(dlsym(nvml_handle_, #name))
#else
    return;
    #define LOAD_NVML(name) nullptr
#endif

#if !defined(__APPLE__)
    auto getCount = LOAD_NVML(nvmlDeviceGetCount);
    auto getHandle = LOAD_NVML(nvmlDeviceGetHandleByIndex);
    auto getTemp = LOAD_NVML(nvmlDeviceGetTemperature);
    auto getPower = LOAD_NVML(nvmlDeviceGetPowerUsage);
    auto getName = LOAD_NVML(nvmlDeviceGetName);

    if (!getCount || !getHandle) return;

    unsigned int count = 0;
    if (getCount(&count) != 0) return;

    for (unsigned int i = 0; i < count; ++i) {
        void* device = nullptr;
        if (getHandle(i, &device) != 0) continue;

        char dev_name[96] = "GPU";
        if (getName) getName(device, dev_name, sizeof(dev_name));

        std::string gpu_label = std::string(dev_name);
        if (count > 1) gpu_label += " #" + std::to_string(i);

        if (getTemp) {
            unsigned int temp = 0;
            if (getTemp(device, 0 /*NVML_TEMPERATURE_GPU*/, &temp) == 0) {
                update_reading(gpu_label + " Temp", "GPU",
                               static_cast<double>(temp), "C");
            }
        }

        if (getPower) {
            unsigned int power_mw = 0;
            if (getPower(device, &power_mw) == 0) {
                update_reading(gpu_label + " Power", "GPU",
                               power_mw / 1000.0, "W");
            }
        }
    }
#endif

    #undef LOAD_NVML
}

// ─── AMD ADL (dynamic loading) ───────────────────────────────────────────────

bool SensorManager::init_adl() {
#if defined(_WIN32)
    adl_handle_ = LoadLibraryA("atiadlxx.dll");
    if (!adl_handle_) {
        adl_handle_ = LoadLibraryA("atiadlxy.dll"); // 32-bit fallback
    }
    if (!adl_handle_) return false;
    return true;

#elif defined(__linux__)
    adl_handle_ = dlopen("libatiadlxx.so", RTLD_NOW);
    if (!adl_handle_) return false;
    return true;

#else
    return false;
#endif
}

void SensorManager::poll_adl() {
    // ADL is complex to fully implement (requires ADL_Main_Control_Create,
    // adapter enumeration, then temperature queries). This is a skeleton
    // that would be expanded with the full ADL SDK.
    //
    // In practice, on Linux with AMD GPUs, the sysfs hwmon backend
    // (amdgpu driver) already provides temperature readings, so ADL
    // is mainly needed on Windows.
    //
    // Full implementation would call:
    //   ADL_Main_Control_Create(malloc_callback, 1)
    //   ADL_Adapter_NumberOfAdapters_Get(&count)
    //   ADL_Overdrive5_Temperature_Get(adapter, 0, &temp)
    //   ADL_Overdrive5_FanSpeed_Get(adapter, 0, &fan)
    (void)adl_handle_;
}

} // namespace occt
