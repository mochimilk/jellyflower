// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "start_properties.hpp"
#include <winrt/Windows.UI.Xaml.Markup.h>
#include <winrt/Windows.UI.Xaml.Shapes.h>
namespace acrylic {
inline sx::Controls::Canvas SearchMagnifier() {
    return sx::Markup::XamlReader::Load(LR"(<Canvas xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" Width="20" Height="20"><Path Data="M17.5,17.5 L13.8833333333,13.8833333333" StrokeThickness="1.6666666667" StrokeStartLineCap="Round" StrokeEndLineCap="Round"/><Ellipse Canvas.Left="2.5" Canvas.Top="2.5" Width="13.3333333333" Height="13.3333333333" StrokeThickness="1.6666666667"/></Canvas>)").as<sx::Controls::Canvas>();
}
}
