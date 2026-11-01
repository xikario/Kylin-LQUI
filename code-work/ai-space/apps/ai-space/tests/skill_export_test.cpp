#include "SkillExport.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDebug>
int main(int argc,char **argv){
 QCoreApplication app(argc,argv);QTemporaryDir temporary;const QString src=temporary.filePath("source"),dst=temporary.filePath("output");QDir().mkpath(src+"/scripts");QDir().mkpath(dst);
 auto write=[](const QString &p,const char *text){QFile f(p);return f.open(QIODevice::WriteOnly)&&f.write(text)>0;};
 write(src+"/SKILL.md","fixture");write(src+"/runtime.py","runtime");auto cancel=std::make_shared<std::atomic_bool>(false);
 for(const auto &target:QStringList{src,src+"/child",src+"/child/ai-space-index-query"})if(SkillExport::publish(src,target,src+"/runtime.py",cancel).isEmpty())return 1;
 if(QFileInfo::exists(src+"/child"))return 2;
 QFile::link(src,temporary.filePath("alias"));if(SkillExport::publish(src,temporary.filePath("alias")+"/inside",src+"/runtime.py",cancel).isEmpty())return 3;
 write(dst+"/keep.txt","previous");
 if(SkillExport::publish(src,dst,src+"/missing.py",cancel).isEmpty()||!QFileInfo::exists(dst+"/keep.txt")||QFileInfo::exists(dst+"/SKILL.md"))return 4;
 if(!SkillExport::publish(src,dst,src+"/runtime.py",cancel).isEmpty()||!QFileInfo::exists(dst+"/keep.txt")||!QFileInfo::exists(dst+"/SKILL.md"))return 5;
 cancel->store(true);if(SkillExport::publish(src,dst,src+"/runtime.py",cancel).isEmpty())return 6;
 qInfo()<<"canonical boundaries, atomic failure, successful merge and cancellation passed";return 0;
}
