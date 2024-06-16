using System.Globalization;
using System.Windows;
using System.Windows.Data;

namespace NetworkProfiler.Converters;


[ValueConversion(typeof(bool), typeof(Visibility))]
public class VisibilityConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        => value switch
        {
            bool visible when visible => Visibility.Visible,
            bool visible when !visible => Visibility.Hidden,
            _ => throw new ArgumentException("not a boolean", nameof(value)),
        };

    public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
        => throw new NotImplementedException();
}
