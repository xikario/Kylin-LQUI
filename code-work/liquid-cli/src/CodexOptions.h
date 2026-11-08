#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace CodexOptions {
inline QJsonObject model(const QJsonArray &models,const QString &id) {
    for(const auto &v:models)if(v.toObject().value("model").toString()==id)return v.toObject();
    return {};
}
inline QString fastTier(const QJsonObject &model) {
    for(const auto &v:model.value("serviceTiers").toArray()){
        const auto id=v.toObject().value("id").toString();
        if(id=="priority"||id=="fast")return id;
    }
    // Older catalogs expose speed ids without display metadata.
    for(const auto &v:model.value("additionalSpeedTiers").toArray())
        if(v.toString()=="fast"||v.toString()=="priority")return v.toString();
    return {};
}
}
