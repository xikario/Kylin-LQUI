#pragma once
#include <functional>
class QString;
class QWidget;
enum class DshService { Aliyun, Official };
void showDshCredentials(QWidget *owner,DshService service,const std::function<bool(const QString &)> &onSaved);
