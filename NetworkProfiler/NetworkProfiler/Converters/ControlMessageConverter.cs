using System.Globalization;
using System.Windows;
using System.Windows.Data;
using NetworkProfiler.Types;

namespace NetworkProfiler.Converters;


[ValueConversion(typeof(ControlMessageEntry), typeof(string))]
public class ControlMessageConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
    {
        if (value is null)
            return string.Empty;

        if (value is not ControlMessageEntry message)
            throw new ArgumentException("not a ControlMessageEntry", nameof(value));

        var paramList = string.Join("\n", message.Params.Split(", "));
        return $"Type={message.MessageType}\nRemote={message.Remote}\n{paramList}";
    }

    public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
        => throw new NotImplementedException();
}
