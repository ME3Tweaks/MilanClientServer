using System.Globalization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using NetworkProfiler.Core;

namespace NetworkProfiler.Converters;


public class RemoteAddressConverter : IMultiValueConverter
{
    public object Convert(object[] values, Type targetType, object parameter, CultureInfo culture)
    {
        object selectedType = values[0];
        object comboControl = values[1];

#pragma warning disable CS8603 // Possible null reference return.
        if (selectedType == DependencyProperty.UnsetValue)
            return null;
        if (selectedType is null)
            return (comboControl as ComboBox)?.Items.GetItemAt(0);
        if (selectedType is string address)
            return address;
#pragma warning restore CS8603 // Possible null reference return.

        throw new NotImplementedException();
    }

    public object[] ConvertBack(object value, Type[] targetTypes, object parameter, CultureInfo culture)
    {
#pragma warning disable CS8625 // Cannot convert null literal to non-nullable reference type.
        if (value is ComboBoxItem item && item.Content is string text && text is "ANY")
            return [null, null];
        if (value is string address)
            return [address, null];
#pragma warning restore CS8625 // Cannot convert null literal to non-nullable reference type.

        throw new NotImplementedException();
    }
}


public class ControlMessageTypeConverter : IMultiValueConverter
{
    public object Convert(object[] values, Type targetType, object parameter, CultureInfo culture)
    {
        object selectedType = values[0];
        object comboControl = values[1];

#pragma warning disable CS8603 // Possible null reference return.
        if (selectedType == DependencyProperty.UnsetValue)
            return null;
        if (selectedType is null)
            return (comboControl as ComboBox)?.Items.GetItemAt(0);
        if (selectedType is ControlMessageType variant)
            return variant;
#pragma warning restore CS8603 // Possible null reference return.

        throw new NotImplementedException();
    }

    public object[] ConvertBack(object value, Type[] targetTypes, object parameter, CultureInfo culture)
    {
#pragma warning disable CS8625 // Cannot convert null literal to non-nullable reference type.
        if (value is ComboBoxItem item && item.Content is string text && text is "ANY")
            return [null, null];
        if (value is ControlMessageType variant)
            return [variant, null];
#pragma warning restore CS8625 // Cannot convert null literal to non-nullable reference type.

        throw new NotImplementedException();
    }
}
