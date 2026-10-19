#pragma once
#include "FigCatalog.h"
#include <QImage>
#include <QPixmap>

//图片资源单例：全项目唯一的图片加载与缓存入口
//内部缓存 QImage（可在工作线程预加载），QPixmap 在首次请求时按需生成（仅 GUI 线程）
//缺图不再弹窗，改为返回占位图并写一条日志
class FigManager
{
private:
	FigManager() = default;

	QHash<int, QImage>  m_images;    //标识 -> 原图，加载失败为空图
	QHash<int, QPixmap> m_pixmaps;   //标识 -> 缓存位图

	//道具图标按"道具名"缓存（名字来自 itemInfo.json，不是编译期枚举）
	//为什么单独一份：道具是可以纯数据新增的（加 JSON 就多一个道具），
	//不可能要求每个新道具都去 Fig 枚举里登记一个值
	QHash<QString, QImage>  m_itemImages;
	QHash<QString, QPixmap> m_itemPixmaps;

	//内部加载，返回是否成功
	bool load(Fig id);
	//内部加载道具图标原图（工作线程安全）
	bool loadItemImage(const QString& itemName);
	//生成/取出占位图
	QPixmap placeholder() const;

public:
	//单例访问
	static FigManager& instance();

	//预加载全部图片（工作线程安全，只碰 QImage 与磁盘）
	void preloadAll();

	//预加载全部道具图标（工作线程安全）：读取 itemInfo.json 里每个道具的 iconPath
	//需要在 ItemDatabase::load() 之后调用；不调用也能工作，只是首次显示时按需加载
	void preloadItemImages();

	//把 QImage 缓存全部转成 QPixmap（只能在 GUI 线程调用）
	//不调用也能工作（首次取图时自动转换），但那会把开销分摊到界面上，
	//表现为"第一次打开商店/背包卡一下"，所以启动收尾时统一预热
	void preloadPixmaps();

	//取图（仅 GUI 线程调用）
	QPixmap fig(Fig id) const;
	QPixmap scaled(Fig id, const QSize& size) const;

	//按道具名取图标（仅 GUI 线程调用）
	//来源优先级：itemInfo.json 的 iconPath > FigCatalog 的登记表 > 占位图
	QPixmap itemIcon(const QString& itemName) const;

	//该道具图标是否真的加载成功（未加载时会按需尝试一次）
	//用途：视图据此决定"画真实图标"还是"退回矢量标记"，
	//因为 itemIcon() 缺图时返回的是占位图，光看返回值分不出来
	bool itemIconReady(const QString& itemName) const;

	//是否真正加载成功（供日志与调试使用）
	bool ready(Fig id) const;
};
