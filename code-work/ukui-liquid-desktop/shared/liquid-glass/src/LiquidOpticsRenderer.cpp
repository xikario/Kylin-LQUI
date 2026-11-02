#include "LiquidMaterial.h"
#include "LiquidOpticsRenderer.h"
#include <QDebug>
#include <QCoreApplication>
#include <QThread>
#include <QElapsedTimer>
#include <QFile>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QPainter>
#include <QPainterPath>
#include <QVector2D>
#include <cmath>

namespace {
// Body diffusion (~7 logical pixels). Keep colour and large features intact.
// The optical rim gets a separate, nearly clear texture below.
using LiquidMaterial::diffuse;

QByteArray resource(const char *path)
{
    QFile file(QString::fromLatin1(path));
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

// Arbitrary magnetic contours: signed distance in logical pixels, encoded in
// RG16 (1/64 px), plus independently antialiased coverage in B. Calculated only
// when shape/geometry changes, never on pointer frames. Original GLSL is kept
// untouched; the adapter uses this field in its distance/normal functions.
QImage shapeField(const QPainterPath &path, QSize pixels, qreal dpr)
{
    if (path.isEmpty()) return {};
    QImage mask(pixels, QImage::Format_ARGB32_Premultiplied);
    mask.fill(Qt::transparent);
    { QPainter p(&mask); p.setRenderHint(QPainter::Antialiasing); p.scale(dpr,dpr); p.fillPath(path,Qt::white); }
    const QPolygonF polygon = path.toFillPolygon();
    struct Segment { qreal x,y,dx,dy,inverseLength; };
    QVector<Segment> segments;
    segments.reserve(polygon.size());
    for (int i=0; i+1<polygon.size(); ++i) {
        const QPointF delta=polygon[i+1]-polygon[i];
        const qreal length=QPointF::dotProduct(delta,delta);
        segments.append({polygon[i].x(),polygon[i].y(),delta.x(),delta.y(),length>0 ? 1.0/length : 0});
    }
    QImage field(pixels, QImage::Format_RGB32);
    for (int y=0; y<pixels.height(); ++y) {
        auto *out = reinterpret_cast<QRgb *>(field.scanLine(y));
        const auto *coverage = reinterpret_cast<const QRgb *>(mask.constScanLine(y));
        for (int x=0; x<pixels.width(); ++x) {
            const qreal px=(x+0.5)/dpr, py=(y+0.5)/dpr;
            // Optical influence ends well before this distance.
            qreal distanceSquared = 128*128;
            for (const auto &edge : segments) {
                const qreal ox=px-edge.x, oy=py-edge.y;
                const qreal t=qBound(0.0,(ox*edge.dx+oy*edge.dy)*edge.inverseLength,1.0);
                const qreal nx=ox-t*edge.dx, ny=oy-t*edge.dy;
                distanceSquared=qMin(distanceSquared,nx*nx+ny*ny);
            }
            const int alpha=qAlpha(coverage[x]);
            const qreal distance=std::sqrt(distanceSquared)*(alpha>=128 ? -1 : 1);
            const int encoded=qBound(0,qRound(32768+distance*64),65535);
            out[x]=qRgb(encoded/256,encoded%256,alpha);
        }
    }
    return field;
}

} // namespace

// GUI-thread-only resources. No live rendering loop and no retained screen
// input textures: only context, linked program and reusable output FBO.
class LiquidOpticsRenderer::Backend {
public:
    static std::shared_ptr<Backend> acquire() {
        Q_ASSERT(QCoreApplication::instance() &&
                 QThread::currentThread() == QCoreApplication::instance()->thread());
        // Weak ownership releases every GL object when the last renderer dies,
        // while QApplication and its display connection still exist.
        static std::weak_ptr<Backend> shared;
        auto backend = shared.lock();
        if (!backend) {
            backend = std::make_shared<Backend>();
            shared = backend;
        }
        return backend;
    }
    ~Backend() {
        const bool current=context.isValid() && context.makeCurrent(&surface);
        framebuffer.reset();
        programStorage.reset();
        if (current) context.doneCurrent();
    }

    QImage render(const QImage &body, const QImage &clear, QSize logical, float radius, const QImage &shape, float refraction, float shade, float highlight, float chroma, float liquidStrength, int control, int finish,float lightSurface,QVector2D smoke) {
        if (!attempted) {
            QElapsedTimer elapsed; elapsed.start();
            attempted=true; ready=initialize();
            if (qEnvironmentVariableIsSet("UKUI_FENCES_STARTUP_TRACE"))
                qInfo() << "[FencesStartup] widget-gl-initialized" << elapsed.elapsed() << "ms" << ready;
        }
        if (!ready || !context.makeCurrent(&surface)) return {};
        const QImage result=draw(body,clear,logical,radius,shape,refraction,shade,highlight,chroma,liquidStrength,control,finish,lightSurface,smoke);
        context.doneCurrent(); // textures were released by draw() before this
        return result;
    }

private:
    bool initialize() {
        QSurfaceFormat format;
        format.setRenderableType(QSurfaceFormat::OpenGL);
        format.setVersion(2,1);
        context.setFormat(format);
        if (!context.create()) return false;
        surface.setFormat(context.format());
        surface.create();
        if (!surface.isValid() || !context.makeCurrent(&surface)) return false;
        context.functions()->initializeOpenGLFunctions();
        programStorage=std::make_unique<QOpenGLShaderProgram>();
        auto &program=*programStorage;
    const QByteArray vertex =
        "#version 120\nattribute vec2 aPosition; varying vec2 uv;\n"
        "void main(){uv=(aPosition+1.0)*0.5; gl_Position=vec4(aPosition,0.0,1.0);}";
    QByteArray upstream=resource(":/liquid-optics/glass.glsl");
    const QByteArray snells=resource(":/liquid-optics/snells-glass.glsl");
    if (upstream.isEmpty() || snells.isEmpty()) {
        context.doneCurrent();
        return false;
    }
    upstream.replace("#include \"snells-glass.glsl\"", snells);
    upstream.replace("float roundedRectangleDist(", "float roundedBoxDist(");
    upstream.replace("vec2 gradSdRoundedBox(", "vec2 boxGradient(");
    const QByteArray fragment = QByteArray(
        "#version 120\n#define texture texture2D\n"
        "uniform sampler2D texUnit; uniform sampler2D bodyUnit; varying vec2 uv;\n"
        "uniform vec2 halfpixel; uniform float viewportScale;\n"
        "uniform vec2 panelSize; uniform float panelRadius; uniform int controlMode;\n"
        "uniform float lightSurface; uniform float shadeStrength; uniform float highlightStrength; uniform float chromaStrength; uniform float liquidStrength;\n"
        "uniform sampler2D shapeUnit; uniform int shaped; uniform int surfaceFinish; uniform vec2 adaptiveSmoke;\n"
        "float roundedRectangleDist(vec2 p,vec2 b,vec4 r);\n"
        "vec2 gradSdRoundedBox(vec2 p,vec2 b,float r);\n") + upstream + R"GLSL(
float contourDistance(vec2 position) {
    vec2 encoded=texture2D(shapeUnit,clamp(position/panelSize+0.5,0.0,1.0)).rg;
    return (dot(encoded,vec2(65280.0,255.0))-32768.0)/64.0;
}
float roundedRectangleDist(vec2 p,vec2 b,vec4 r) {
    return shaped>0 ? contourDistance(p) : roundedBoxDist(p,b,r);
}
vec2 gradSdRoundedBox(vec2 p,vec2 b,float r) {
    if(shaped==0) return boxGradient(p,b,r);
    return vec2(contourDistance(p+vec2(1.0,0.0))-contourDistance(p-vec2(1.0,0.0)),
                contourDistance(p+vec2(0.0,1.0))-contourDistance(p-vec2(0.0,1.0)))*0.5;
}
void main() {
    vec2 halfSize=panelSize*0.5;
    vec2 position=uv*panelSize-halfSize;
    vec4 radii=vec4(panelRadius);
    float d=roundedRectangleDist(position,halfSize,radii);
    float aa=max(fwidth(d),0.75);
    // Preserve upstream Snell optics but separate them from diffuse content.
    float inside=max(-d,0.0);
    float edgeFactor=1.0-clamp(inside/edgeSizePixels,0.0,1.0);
    float concave=1.0-sqrt(max(0.0,1.0-pow(smoothstep(0.0,1.0,edgeFactor),refractionNormalPow)));
    vec4 normalRadii=clamp(radii*2.0,min(64.0,min(halfSize.x,halfSize.y)),min(128.0,min(halfSize.x,halfSize.y)));
    if(controlMode>0)
        normalRadii=vec4(min(panelRadius*1.5,min(halfSize.x,halfSize.y)*0.9));
    vec3 optical=texture2D(texUnit,uv).rgb;
    if(surfaceFinish==0 || refractionStrength>0.001)
        optical=snellsRefraction(position,halfSize,normalRadii,
            min(halfSize.x,halfSize.y),d,concave).color.rgb;
    float clearRim=1.0-smoothstep(3.0,min(40.0,min(halfSize.x,halfSize.y)*0.8),inside);
    if(liquidStrength!=1.0) {
        float width=mix(0.35,1.65,clamp(liquidStrength/2.0,0.0,1.0));
        clearRim=(1.0-smoothstep(3.0*width,min(40.0,min(halfSize.x,halfSize.y)*0.8)*width,inside))
                 *min(liquidStrength,1.0);
    }
    if(surfaceFinish==3)clearRim=0.0;
    vec3 rgb=mix(texture2D(bodyUnit,uv).rgb,optical,clearRim);
    float lum=dot(rgb,vec3(0.299,0.587,0.114));
    // Neutral adaptive scrim: no blue pigment or near-opaque graphite fill.
    // White reaches ~145/255 in the body, clear edges keep 88% transmission.
    float scrim=mix(0.16+0.27*smoothstep(0.25,0.95,lum),0.12,clearRim);
    rgb=mix(vec3(lum),rgb,chromaStrength)*(1.0-scrim*shadeStrength*mix(1.0,0.15,lightSurface));
    // Nested glass samples the already-rendered panel, never icons/text.
    // Do not apply the panel's dark scrim or saturation a second time.
    if(controlMode>0) {
        rgb=mix(texture2D(bodyUnit,uv).rgb,optical,1.0-smoothstep(2.0,12.0,inside));
        rgb=mix(rgb,vec3(1.0),controlMode==2 ? 0.035 : 0.075);
        if(controlMode==2) rgb*=0.91;
    }
    vec3 soft=applySoftMaterial(rgb,position,halfSize,radii,d,edgeFactor);
    rgb=mix(soft,rgb,lightSurface*0.82);
    // NextKde's clear/soft presets disable broad reflection. The Dock uses
    // one bounded edge contribution, rather than extrapolating the glints and
    // adding a second bright caustic over the same upper-left corner.
    float glintWeight=surfaceFinish>0 ? clamp(highlightStrength,0.0,1.0) : highlightStrength;
    if(surfaceFinish!=1)rgb=mix(rgb,applyLiquidGlints(rgb,position,halfSize,radii,d,aa),glintWeight*mix(1.0,0.25,lightSurface));
    // Inner caustic: directional, curved, fades inward rather than a flat frame.
    vec2 normal=gradSdRoundedBox(position,halfSize,panelRadius);
    float facing=pow(max(dot(normalize(normal+vec2(0.0001)),
        normalize(vec2(-0.65,0.76))),0.0),3.0);
    float caustic=exp(-pow((inside-4.0)/2.4,2.0));
    if(surfaceFinish==0)rgb+=vec3(0.105,0.12,0.14)*caustic*facing*highlightStrength*mix(1.0,0.25,lightSurface);
    if(surfaceFinish==1) {
        // Low-height clear glass: a curved, continuous rim instead of a
        // broad corner hotspot. No refraction is needed for this finish.
        float rim=exp(-pow((inside-1.35)/1.15,2.0));
        float inner=exp(-pow((inside-4.2)/2.0,2.0));
        float shine=clamp(highlightStrength,0.0,1.0)*(rim*(0.20+0.30*facing)+inner*0.06);
        rgb=mix(rgb,vec3(0.98,0.995,1.0),shine);
        rgb*=1.0-rim*(1.0-facing)*0.04;
    }
    if(adaptiveSmoke.x>0.0 && controlMode==0) {
        // One luminance decision for the whole card. Preserve colour/detail
        // with a bounded slope, instead of a separate gray/black material on
        // each side of a bright-window boundary. Keep Snell only in a fine rim.
        float narrowRim=1.0-smoothstep(0.8,5.0,inside);
        vec3 scene=mix(texture2D(bodyUnit,uv).rgb,optical,narrowRim*0.35);
        vec3 smoke=vec3(0.22+0.10*adaptiveSmoke.y)+(scene-vec3(adaptiveSmoke.y))*0.22;
        float line=exp(-pow((inside-1.0)/0.75,2.0));
        float inner=exp(-pow((inside-3.2)/1.6,2.0));
        smoke+=vec3(0.16,0.18,0.20)*line*(0.55+0.45*facing);
        smoke+=vec3(0.025)*inner*facing;
        rgb=mix(rgb,smoke,adaptiveSmoke.x);
    }
    float coverage=1.0-smoothstep(-aa*0.5,aa*0.5,d);
    if(shaped>0) coverage=texture2D(shapeUnit,uv).b;
    // QOpenGLFramebufferObject::toImage returns premultiplied ARGB.
    // Straight RGB at alpha=0 caused coloured square-corner leaks in QPainter.
    gl_FragColor=vec4(clamp(rgb,0.0,1.0)*coverage,coverage);
})GLSL";
    if (!program.addCacheableShaderFromSourceCode(QOpenGLShader::Vertex,vertex)
        || !program.addCacheableShaderFromSourceCode(QOpenGLShader::Fragment,fragment)
        || !program.link()) {
        qWarning() << "[NextKdeGlass] shader fallback:" << program.log();
        context.doneCurrent();
        return false;
    }
    context.doneCurrent();
    return true;
    }

    QImage draw(const QImage &body,const QImage &clear,QSize logical,float radius,const QImage &shape, float refraction, float shade, float highlight, float chroma, float liquidStrength, int control, int finish,float lightSurface,QVector2D smoke) {
    auto *gl=context.functions();
    auto &program=*programStorage;
    if (!framebuffer || framebuffer->size()!=body.size())
        framebuffer=std::make_unique<QOpenGLFramebufferObject>(body.size());
    auto &fbo=*framebuffer;
    if (!fbo.isValid() || !fbo.bind()) return {};
    QOpenGLTexture texture(clear.mirrored(), QOpenGLTexture::DontGenerateMipMaps);
    QOpenGLTexture bodyTexture(body.mirrored(), QOpenGLTexture::DontGenerateMipMaps);
    QImage shapeInput=shape;
    if (shapeInput.isNull()) { shapeInput=QImage(1,1,QImage::Format_RGB32); shapeInput.fill(Qt::black); }
    QOpenGLTexture shapeTexture(shapeInput.mirrored(), QOpenGLTexture::DontGenerateMipMaps);
    if (!texture.isCreated() || !bodyTexture.isCreated() || !shapeTexture.isCreated()) { fbo.release(); return {}; }
    texture.setMinMagFilters(QOpenGLTexture::Linear,QOpenGLTexture::Linear);
    texture.setWrapMode(QOpenGLTexture::ClampToEdge);
    bodyTexture.setMinMagFilters(QOpenGLTexture::Linear,QOpenGLTexture::Linear);
    bodyTexture.setWrapMode(QOpenGLTexture::ClampToEdge);
    shapeTexture.setMinMagFilters(QOpenGLTexture::Linear,QOpenGLTexture::Linear);
    shapeTexture.setWrapMode(QOpenGLTexture::ClampToEdge);
    shapeTexture.bind(2);
    bodyTexture.bind(1);
    texture.bind(0);
    gl->glViewport(0,0,body.width(),body.height());
    gl->glDisable(GL_BLEND);
    gl->glDisable(GL_DEPTH_TEST);
    program.bind();
    program.setUniformValue("texUnit",0);
    program.setUniformValue("bodyUnit",1);
    program.setUniformValue("shapeUnit",2);
    program.setUniformValue("shaped",shape.isNull() ? 0 : 1);
    program.setUniformValue("panelSize",QVector2D(logical.width(),logical.height()));
    program.setUniformValue("halfpixel",QVector2D(1.f/logical.width(),1.f/logical.height()));
    program.setUniformValue("viewportScale",1.f);
    program.setUniformValue("panelRadius",radius);
    program.setUniformValue("controlMode",control);
    program.setUniformValue("surfaceFinish",finish);
    program.setUniformValue("edgeSizePixels",control ? 5.5f : qMin(22.f,qMin(logical.width(),logical.height())*.24f));
    const bool dock=finish>0&&!control;
    const float lens=dock ? (finish==3?0.f:qMin(1.f,liquidStrength)*(finish==2?.28f:qMin(1.f,refraction/3.5f))) : 1.f;
    program.setUniformValue("refractionStrength",lens);
    program.setUniformValue("refractionNormalPow",dock?5.f:2.f);
    program.setUniformValue("refractionRGBFringing",dock?0.f:control ? 0.35f : 0.65f);
    program.setUniformValue("refractionOffsetStrength",dock?(finish==1?refraction*liquidStrength:0.f):control ? 0.55f : refraction*liquidStrength);
    program.setUniformValue("liquidStrength",liquidStrength);
    program.setUniformValue("materialSoftness",dock?(finish==2?.65f:finish==3?1.f:0.f):0.f);
    program.setUniformValue("materialReflectionStrength",dock?(finish==3?.12f:0.f):control ? 0.28f : 0.14f);
    program.setUniformValue("cornerExponent",2.f);
    program.setUniformValue("lightSurface",lightSurface);
    program.setUniformValue("adaptiveSmoke",smoke);
    program.setUniformValue("shadeStrength",shade/.58f);
    program.setUniformValue("highlightStrength",dock?highlight:highlight/.55f);
    program.setUniformValue("chromaStrength",chroma);
    const GLfloat triangle[]={-1,-1,3,-1,-1,3};
    const int loc=program.attributeLocation("aPosition");
    program.enableAttributeArray(loc);
    program.setAttributeArray(loc,GL_FLOAT,triangle,2);
    gl->glDrawArrays(GL_TRIANGLES,0,3);
    program.disableAttributeArray(loc);
    const QImage result=fbo.toImage();
    program.release();
    fbo.release();
    return result;
    }

    QOffscreenSurface surface;
    QOpenGLContext context;
    std::unique_ptr<QOpenGLShaderProgram> programStorage;
    std::unique_ptr<QOpenGLFramebufferObject> framebuffer;
    bool attempted=false;
    bool ready=false;
};

static void initializeOpticsResources() { Q_INIT_RESOURCE(liquid_optics); }
LiquidOpticsRenderer::LiquidOpticsRenderer() { initializeOpticsResources(); }
void LiquidOpticsRenderer::setOptics(qreal refraction,qreal shade,qreal highlight,qreal chroma) {
    m_refraction=qBound(0.,refraction,8.);m_shade=qBound(.15,shade,.95);m_highlight=qBound(0.,highlight,1.);
    m_chroma=qBound(0.,chroma,1.3);
}
void LiquidOpticsRenderer::setMaterial(qreal clarity, qreal liquidStrength) {
    m_clarity=qBound(0.,clarity,1.);
    m_liquidStrength=qBound(0.,liquidStrength,2.);
}
LiquidOpticsRenderer::~LiquidOpticsRenderer() = default;

void LiquidOpticsRenderer::setWallpaper(const QImage &source)
{
    if (source.cacheKey() == m_source.cacheKey()
        && source.devicePixelRatio() == m_source.devicePixelRatio()) return;
    m_source = source;
    m_body = {};
    m_clear = {};
}

void LiquidOpticsRenderer::setPreparedWallpaper(const LiquidMaterial::Prepared &material)
{
    m_source = material.source;
    m_body = material.body;
    m_clear = material.clear;
    ++m_preparationCount;
}

QImage LiquidOpticsRenderer::renderPanel(const QRect &logicalRect, qreal radius, const QPainterPath &shape)
{
    return renderSurface(logicalRect,radius,shape,0);
}
QImage LiquidOpticsRenderer::renderControl(const QRect &logicalRect, qreal radius, bool pressed)
{
    return renderSurface(logicalRect,radius,{},pressed?2:1);
}
QImage LiquidOpticsRenderer::renderSurface(const QRect &logicalRect, qreal radius, const QPainterPath &shape, int control)
{
    m_usedGpu = false;m_smokeAmount=0;
    if (m_source.isNull() || logicalRect.isEmpty()) return {};
    const qreal dpr = m_source.devicePixelRatio();
    if (m_body.isNull() && !control) {
        const QSize logical(qMax(1, qRound(m_source.width()/dpr)),
                            qMax(1, qRound(m_source.height()/dpr)));
        m_body = diffuse(m_source, logical);
        m_clear = diffuse(m_source, logical, 1, 2);
        m_body.setDevicePixelRatio(1);
        m_clear.setDevicePixelRatio(1);
        ++m_preparationCount;
    }
    const QRect pixels(qRound(logicalRect.x()*dpr), qRound(logicalRect.y()*dpr),
                       qMax(1, qRound(logicalRect.width()*dpr)),
                       qMax(1, qRound(logicalRect.height()*dpr)));
    QImage body = (control?m_source:m_body).copy(pixels);
    QImage clear = (control?m_source:m_clear).copy(pixels);
    body.setDevicePixelRatio(1);clear.setDevicePixelRatio(1);
    if(m_clarity>0) {
        QImage raw=m_source.copy(pixels);raw.setDevicePixelRatio(1);
        { QPainter p(&body);p.setOpacity(m_clarity);p.drawImage(0,0,raw); }
        { QPainter p(&clear);p.setOpacity(m_clarity);p.drawImage(0,0,raw); }
    }
    qreal sceneMean=0,brightArea=0;
    if(m_adaptiveSmoke&&!control) {
        // Fixed 16x16 sampling cost, independent of DPR and card height.
        for(int y=0;y<16;++y)for(int x=0;x<16;++x) {
            const QColor c=body.pixelColor(qMin(body.width()-1,int((x+.5)*body.width()/16)),
                                           qMin(body.height()-1,int((y+.5)*body.height()/16)));
            const qreal luminance=.299*c.redF()+.587*c.greenF()+.114*c.blueF();
            sceneMean+=luminance/256.;
            const qreal bright=qBound(0.,(luminance-.45)/.40,1.);
            brightArea+=bright*bright*(3-2*bright)/256.;
        }
        // Bright coverage, not a dark wallpaper's mean, activates the finish.
        // A half-white card gets one coherent shell; dark colours stay intact.
        const qreal t=qBound(0.,brightArea/.45,1.);
        m_smokeAmount=t*t*(3-2*t);
    }
    // Radius must fit collapsed title-only fences too.
    radius = qMin(radius, qMin(logicalRect.width(), logicalRect.height())/2.0);
    QImage result;
    if (!qEnvironmentVariableIsSet("UKUI_LIQUID_GLASS_NO_GL")) {
        if (!m_backend) m_backend = Backend::acquire();
        if (!shape.isEmpty() && (m_shapeField.isNull() || m_cachedShape != shape ||
            m_shapeSize != pixels.size() || !qFuzzyCompare(m_shapeDpr, dpr))) {
            m_shapeField = shapeField(shape, pixels.size(), dpr);
            m_cachedShape = shape;
            m_shapeSize = pixels.size();
            m_shapeDpr = dpr;
        }
        result = m_backend->render(body, clear, logicalRect.size(), radius,
                                  shape.isEmpty() ? QImage() : m_shapeField, m_refraction, m_shade, m_highlight, m_chroma, m_liquidStrength, control,int(m_finish),m_lightSurface,QVector2D(m_smokeAmount,sceneMean));
    }
    m_usedGpu = !result.isNull();
    if (!m_usedGpu) {
        // Readable, antialiased fallback, explicitly without Snell refraction.
        result = QImage(pixels.size(), QImage::Format_ARGB32_Premultiplied);
        result.fill(Qt::transparent);
        QPainter p(&result);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        if (shape.isEmpty())
            path.addRoundedRect(QRectF(QPointF(), QSizeF(result.size())), radius*dpr, radius*dpr);
        else
            path=QTransform::fromScale(dpr,dpr).map(shape);
        if(m_finish!=Finish::Frosted && (m_finish!=Finish::Original || !qFuzzyCompare(m_liquidStrength,1.0))) {
            // Bounded row sampling, only on cache rebuild. CPU mode still gives
            // the strength control visible curvature, without a render loop.
            const QImage input=body;
            const qreal band=qMax(1.,qMin(22.,qMin(logicalRect.width(),logicalRect.height())*.24)*dpr);
            for(int y=0;y<body.height();++y) {
                auto *out=reinterpret_cast<QRgb *>(body.scanLine(y));
                for(int x=0;x<body.width();++x) {
                    const qreal edge=qBound(0.,1.-qMin(qMin(x,body.width()-1-x),qMin(y,body.height()-1-y))/band,1.);
                    const qreal bend=m_refraction*(m_finish==Finish::Original?m_liquidStrength-1.:m_liquidStrength*.22)*dpr*edge*edge;
                    const int sx=qBound(0,qRound(x+(x<body.width()/2?bend:-bend)),body.width()-1);
                    const int sy=qBound(0,qRound(y+(y<body.height()/2?bend:-bend)),body.height()-1);
                    out[x]=input.pixel(sx,sy);
                }
            }
        }
        if(m_finish!=Finish::Original&&!control) {
            for(int y=0;y<body.height();++y) {
                auto *line=reinterpret_cast<QRgb *>(body.scanLine(y));
                for(int x=0;x<body.width();++x) {
                    const QRgb c=line[x];const qreal luma=.299*qRed(c)+.587*qGreen(c)+.114*qBlue(c);
                    auto channel=[&](int value){return qBound(0,qRound(luma+(value-luma)*m_chroma),255);};
                    line[x]=qRgb(channel(qRed(c)),channel(qGreen(c)),channel(qBlue(c)));
                }
            }
        }
        p.drawImage(0, 0, body);
        p.fillRect(result.rect(), control ? (control==2?QColor(0,0,0,20):QColor(255,255,255,20)) : QColor(0, 0, 0, qRound(180*m_shade*(1.0-.85*m_lightSurface))));
        if(m_finish!=Finish::Original&&!control) {
            // Cache-only CPU finish follows the same restrained inset sheen;
            // no saturated diagonal corner or full bright outline.
            p.save();p.setClipPath(path);
            const qreal softness=m_finish==Finish::Liquid?0.:m_finish==Finish::Soft?.65:1.;
            const qreal band=qMin(result.height()/2.,(3.+8.*softness)*dpr);
            QLinearGradient glow(0,0,0,band);
            glow.setColorAt(0,QColor(239,248,255,qRound(48*m_highlight)));
            glow.setColorAt(1,Qt::transparent);p.fillRect(QRectF(0,0,result.width(),band),glow);
            const qreal inset=radius*dpr+3*dpr;
            QLinearGradient line(inset,0,result.width()-inset,0);
            line.setColorAt(0,Qt::transparent);line.setColorAt(.5,QColor(245,250,255,qRound(80*m_highlight)));line.setColorAt(1,Qt::transparent);
            p.setPen(QPen(QBrush(line),.8*dpr));p.drawLine(QPointF(inset,1.2*dpr),QPointF(result.width()-inset,1.2*dpr));p.restore();
            if(m_finish==Finish::Liquid) {
                QLinearGradient rim(QPointF(),QPointF(result.width(),result.height()));
                rim.setColorAt(0,QColor(245,250,255,qRound(180*m_highlight)));rim.setColorAt(.45,QColor(232,242,255,qRound(32*m_highlight)));rim.setColorAt(1,QColor(245,250,255,qRound(100*m_highlight)));
                p.save();p.setClipPath(path);p.setBrush(Qt::NoBrush);p.setPen(QPen(QBrush(rim),1.2*dpr));p.drawPath(path);p.restore();
            }
        }
        QImage mask(result.size(), QImage::Format_ARGB32_Premultiplied);
        mask.fill(Qt::transparent);
        { QPainter mp(&mask); mp.setRenderHint(QPainter::Antialiasing); mp.fillPath(path, Qt::white); }
        p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        p.drawImage(0, 0, mask);
    }
    if(!m_usedGpu&&m_smokeAmount>0) {
        // The optional CPU fallback uses the same card-wide tone curve and
        // alpha coverage. No extra capture, blur or recurring rendering work.
        for(int y=0;y<result.height();++y) {
            auto *line=reinterpret_cast<QRgb *>(result.scanLine(y));
            for(int x=0;x<result.width();++x) {
                const QRgb old=line[x];const int alpha=qAlpha(old);if(!alpha)continue;
                const QRgb scene=body.pixel(x,y);
                const qreal qx=qAbs((x+.5)/dpr-logicalRect.width()/2.)-(logicalRect.width()/2.-radius);
                const qreal qy=qAbs((y+.5)/dpr-logicalRect.height()/2.)-(logicalRect.height()/2.-radius);
                const qreal inside=qMax(0.,radius-std::hypot(qMax(0.,qx),qMax(0.,qy))-qMin(qMax(qx,qy),0.));
                const qreal sheen=std::exp(-std::pow((inside-1.)/.75,2.))*.12;
                auto channel=[&](int oldValue,int sourceValue){
                    const qreal smoke=.22+.10*sceneMean+(sourceValue/255.-sceneMean)*.22+sheen;
                    return qBound(0,qRound(oldValue*(1-m_smokeAmount)+smoke*alpha*m_smokeAmount),alpha);
                };
                line[x]=qRgba(channel(qRed(old),qRed(scene)),channel(qGreen(old),qGreen(scene)),channel(qBlue(old),qBlue(scene)),alpha);
            }
        }
    }
    result.setDevicePixelRatio(dpr);
    return result;
}
