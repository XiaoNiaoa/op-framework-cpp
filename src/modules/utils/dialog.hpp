#pragma once

#include <functional>
#include <string>
#include <string_view>

#include <sdk.hpp>
#include <Server/Components/Dialogs/dialogs.hpp>

namespace OPUtil::Dialog
{
struct Response
{
    bool accepted = false;
    int listItem = -1;
    StringView inputText;
};

using Callback = std::function<void(IPlayer& player, const Response& response)>;

void show(IPlayer& player, DialogStyle style, StringView title, StringView body, StringView button1, StringView button2, Callback onResponse);

/// 在正文上方加一行红色提示（notice 为空则原样返回）。
std::string withNotice(std::string_view notice, std::string body);
}
