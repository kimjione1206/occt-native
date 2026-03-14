using System.Text.Json;
using System.Text.Json.Serialization;
using LibreHardwareMonitor.Hardware;

namespace LhmSensorReader;

public class Program
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
        WriteIndented = false
    };

    public static int Main(string[] args)
    {
        bool jsonOutput = false;
        bool once = false;
        int loopMs = 0;

        for (int i = 0; i < args.Length; i++)
        {
            switch (args[i])
            {
                case "--json":
                    jsonOutput = true;
                    break;
                case "--once":
                    once = true;
                    break;
                case "--loop":
                    if (i + 1 < args.Length && int.TryParse(args[i + 1], out int ms))
                    {
                        loopMs = ms;
                        i++;
                    }
                    break;
            }
        }

        // Default: same as --json --once
        if (!once && loopMs <= 0)
        {
            once = true;
            jsonOutput = true;
        }

        var computer = new Computer
        {
            IsCpuEnabled = true,
            IsGpuEnabled = true,
            IsMotherboardEnabled = true,
            IsStorageEnabled = true,
            IsMemoryEnabled = true
        };

        try
        {
            computer.Open();
        }
        catch (Exception)
        {
            var error = new { error = "admin_required", message = "Run as Administrator for hardware sensor access" };
            Console.WriteLine(JsonSerializer.Serialize(error, JsonOptions));
            return 2;
        }

        try
        {
            if (once)
            {
                var output = CollectSensorData(computer);
                Console.WriteLine(JsonSerializer.Serialize(output, JsonOptions));
                return 0;
            }

            // Loop mode
            using var cts = new CancellationTokenSource();
            Console.CancelKeyPress += (_, e) =>
            {
                e.Cancel = true;
                cts.Cancel();
            };

            while (!cts.Token.IsCancellationRequested)
            {
                var output = CollectSensorData(computer);
                try
                {
                    Console.WriteLine(JsonSerializer.Serialize(output, JsonOptions));
                    Console.Out.Flush();
                }
                catch (IOException)
                {
                    // stdout closed (pipe broken)
                    break;
                }

                try
                {
                    Task.Delay(loopMs, cts.Token).Wait();
                }
                catch (AggregateException ex) when (ex.InnerException is TaskCanceledException)
                {
                    break;
                }
            }

            return 0;
        }
        finally
        {
            computer.Close();
        }
    }

    private static SensorOutput CollectSensorData(Computer computer)
    {
        var hardwareList = new List<HardwareEntry>();

        foreach (var hw in computer.Hardware)
        {
            hw.Update();
            hardwareList.Add(BuildHardwareEntry(hw));

            foreach (var sub in hw.SubHardware)
            {
                sub.Update();
                hardwareList.Add(BuildHardwareEntry(sub));
            }
        }

        return new SensorOutput { Hardware = hardwareList };
    }

    private static HardwareEntry BuildHardwareEntry(IHardware hw)
    {
        var sensors = new List<SensorEntry>();

        foreach (var sensor in hw.Sensors)
        {
            if (sensor.Value is null)
                continue;

            var entry = new SensorEntry
            {
                Name = sensor.Name,
                Type = sensor.SensorType.ToString(),
                Value = Math.Round(sensor.Value.Value, 1),
                Unit = MapUnit(sensor.SensorType)
            };

            if (sensor.Min.HasValue)
                entry.Min = Math.Round(sensor.Min.Value, 1);
            if (sensor.Max.HasValue)
                entry.Max = Math.Round(sensor.Max.Value, 1);

            sensors.Add(entry);
        }

        return new HardwareEntry
        {
            Name = hw.Name,
            Type = MapHardwareType(hw.HardwareType),
            Sensors = sensors
        };
    }

    private static string MapUnit(SensorType type) => type switch
    {
        SensorType.Temperature => "C",
        SensorType.Power => "W",
        SensorType.Voltage => "V",
        SensorType.Fan => "RPM",
        SensorType.Load => "%",
        SensorType.Clock => "MHz",
        SensorType.Data => "GB",
        SensorType.SmallData => "MB",
        SensorType.Throughput => "MB/s",
        _ => ""
    };

    private static string MapHardwareType(HardwareType type) => type switch
    {
        HardwareType.Cpu => "CPU",
        HardwareType.GpuNvidia => "GPU",
        HardwareType.GpuAmd => "GPU",
        HardwareType.GpuIntel => "GPU",
        HardwareType.Motherboard => "Motherboard",
        HardwareType.Storage => "Storage",
        HardwareType.Memory => "RAM",
        _ => type.ToString()
    };
}

public class SensorOutput
{
    [JsonPropertyName("hardware")]
    public List<HardwareEntry> Hardware { get; set; } = new();
}

public class HardwareEntry
{
    [JsonPropertyName("name")]
    public string Name { get; set; } = "";

    [JsonPropertyName("type")]
    public string Type { get; set; } = "";

    [JsonPropertyName("sensors")]
    public List<SensorEntry> Sensors { get; set; } = new();
}

public class SensorEntry
{
    [JsonPropertyName("name")]
    public string Name { get; set; } = "";

    [JsonPropertyName("type")]
    public string Type { get; set; } = "";

    [JsonPropertyName("value")]
    public double Value { get; set; }

    [JsonPropertyName("unit")]
    public string Unit { get; set; } = "";

    [JsonPropertyName("min")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public double? Min { get; set; }

    [JsonPropertyName("max")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public double? Max { get; set; }
}
