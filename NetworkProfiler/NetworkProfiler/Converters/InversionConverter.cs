using System.Globalization;
using System.Windows;
using System.Windows.Data;

namespace NetworkProfiler.Converters;


[ValueConversion(typeof(bool), typeof(bool))]
public class InversionConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        => value switch
        {
            bool visible => !visible,
            object other => other
        };

    public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
        => throw new NotImplementedException();
}
