#include "FigManager.h"
#include "DirectoryLayout.h"
#include "FileManager.h"
#include "ItemDatabase.h"
#include <QFile>
#include <QPainter>
#include <QStringList>

//单例访问
FigManager& FigManager::instance()
{
	static FigManager s_instance;
	return s_instance;
}

//内部加载：磁盘缺图时回落到 Qt 资源
bool FigManager::load(Fig id)
{
	int key = static_cast<int>(id);
	if (m_images.contains(key)) { return !m_images.value(key).isNull(); }

	QString relative = FigCatalog::relativePath(id);
	if (relative.isEmpty())
	{
		FileManager::instance().log("fig", QString("未登记的图片标识: %1").arg(key));
		m_images.insert(key, QImage());
		return false;
	}

	QString fullPath = DirectoryLayout::instance().assetsDir() + "/" + relative;
	QImage image;
	if (QFile::exists(fullPath))
	{
		image = QImage(fullPath);
	}

	//磁盘没有时尝试资源内置副本（:/Assets/picture/xxx.png）
	if (image.isNull())
	{
		QString qrcPath = ":/Assets/" + relative;
		if (QFile::exists(qrcPath)) { image = QImage(qrcPath); }
	}

	if (image.isNull())
	{
		FileManager::instance().log("fig", QString("图片缺失: %1").arg(fullPath));
	}

	m_images.insert(key, image);
	return !image.isNull();
}

//内部加载道具图标原图：磁盘优先，其次资源内置副本
//候选路径要试三种写法，因为 itemInfo.json 里的 iconPath 是"相对运行根目录"的
//（例如 Assets/picture/liveBottle.png），而 FigCatalog 登记的是"相对 Assets"的
bool FigManager::loadItemImage(const QString& itemName)
{
	if (itemName.isEmpty()) { return false; }
	if (m_itemImages.contains(itemName)) { return !m_itemImages.value(itemName).isNull(); }

	QImage image;

	//① 数据驱动：itemInfo.json 里的 iconPath
	const QString relative = ItemDatabase::instance().iconPath(itemName);
	if (!relative.isEmpty())
	{
		QStringList candidates;
		candidates << ItemDatabase::absoluteIconPath(relative);                        // 相对运行根目录
		candidates << DirectoryLayout::instance().assetsDir() + "/" + relative;        // 相对 Assets
		candidates << ":/" + relative;                                                 // qrc 内置副本
		candidates << ":/Assets/" + relative;

		for (const QString& path : candidates)
		{
			if (!QFile::exists(path)) { continue; }
			image = QImage(path);
			if (!image.isNull()) { break; }
		}
	}

	//② 回落到登记表（FigCatalog）：JSON 里图标路径写错或没写时仍然能显示
	if (image.isNull())
	{
		Fig fig;
		if (FigCatalog::itemFig(itemName, fig))
		{
			const QString path = FigCatalog::relativePath(fig);
			const QString onDisk = DirectoryLayout::instance().assetsDir() + "/" + path;
			const QString inQrc = ":/Assets/" + path;

			if (QFile::exists(onDisk)) { image = QImage(onDisk); }
			if (image.isNull() && QFile::exists(inQrc)) { image = QImage(inQrc); }
		}
	}

	if (image.isNull())
	{
		FileManager::instance().log("fig", QString("道具图标缺失: %1").arg(itemName));
	}

	m_itemImages.insert(itemName, image);
	return !image.isNull();
}

//占位图：灰底加斜线，一眼能看出图片缺失
QPixmap FigManager::placeholder() const
{
	static QPixmap s_placeholder;
	if (!s_placeholder.isNull()) { return s_placeholder; }

	s_placeholder = QPixmap(32, 32);
	s_placeholder.fill(QColor(180, 180, 180));

	QPainter painter(&s_placeholder);
	painter.setPen(QColor(120, 120, 120));
	painter.drawLine(0, 0, 31, 31);
	painter.drawLine(31, 0, 0, 31);
	painter.end();

	return s_placeholder;
}

//取出位图（首次访问时才把 QImage 转成 QPixmap）
QPixmap FigManager::fig(Fig id) const
{
	int key = static_cast<int>(id);

	//缓存命中
	if (m_pixmaps.contains(key)) { return m_pixmaps.value(key); }

	//未加载过则先加载（const 方法里用 const_cast 更新缓存）
	FigManager* self = const_cast<FigManager*>(this);
	self->load(id);

	QPixmap pixmap;
	const QImage image = m_images.value(key);
	if (!image.isNull()) { pixmap = QPixmap::fromImage(image); }
	if (pixmap.isNull()) { pixmap = placeholder(); }

	//写入位图缓存（const 方法，用 self 这个非 const 指针）
	self->m_pixmaps.insert(key, pixmap);
	return pixmap;
}

//按尺寸缩放取图
QPixmap FigManager::scaled(Fig id, const QSize& size) const{
	QPixmap source = fig(id);
	if (size.isEmpty()) { return source; }
	return source.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

//按道具名取图标（首次访问时才把 QImage 转成 QPixmap）
QPixmap FigManager::itemIcon(const QString& itemName) const
{
	if (m_itemPixmaps.contains(itemName)) { return m_itemPixmaps.value(itemName); }

	FigManager* self = const_cast<FigManager*>(this);
	self->loadItemImage(itemName);

	QPixmap pixmap;
	const QImage image = m_itemImages.value(itemName);
	if (!image.isNull()) { pixmap = QPixmap::fromImage(image); }
	if (pixmap.isNull()) { pixmap = placeholder(); }

	self->m_itemPixmaps.insert(itemName, pixmap);
	return pixmap;
}

//道具图标是否真的加载成功（未加载时按需尝试一次）
bool FigManager::itemIconReady(const QString& itemName) const
{
	if (m_itemImages.contains(itemName))
	{
		return !m_itemImages.value(itemName).isNull();
	}

	FigManager* self = const_cast<FigManager*>(this);
	return self->loadItemImage(itemName);
}

//是否加载成功
bool FigManager::ready(Fig id) const
{
	int key = static_cast<int>(id);

	if (!m_images.contains(key))
	{
		FigManager* self = const_cast<FigManager*>(this);
		return self->load(id);
	}
	return !m_images.value(key).isNull();
}

//预加载全部图片
void FigManager::preloadAll()
{
	const Fig all[] = {
		Fig::Player, Fig::Enemy, Fig::Bomb, Fig::LiveBottle, Fig::QuickBoot,
		Fig::BombUpper, Fig::SuperBag, Fig::StringUnravelEye, Fig::StringUnravelEyeBroken,
		Fig::BonVoyagingStar, Fig::BonVoyagingStarBroken
	};

	int okCount = 0;
	for (Fig id : all)
	{
		if (load(id)) { ++okCount; }
	}

	FileManager::instance().log("fig", QString("图片预加载完成: %1/%2").arg(okCount).arg(sizeof(all) / sizeof(all[0])));
}

//预加载全部道具图标（工作线程安全：只碰 QImage 与磁盘）
void FigManager::preloadItemImages()
{
	const QStringList names = ItemDatabase::instance().itemNames();

	int okCount = 0;
	for (const QString& name : names)
	{
		if (loadItemImage(name)) { ++okCount; }
	}

	FileManager::instance().log("fig", QString("道具图标预加载完成: %1/%2").arg(okCount).arg(names.size()));
}

//把 QImage 缓存全部转成 QPixmap（仅 GUI 线程）
void FigManager::preloadPixmaps()
{
	const Fig all[] = {
		Fig::Player, Fig::Enemy, Fig::Bomb, Fig::LiveBottle, Fig::QuickBoot,
		Fig::BombUpper, Fig::SuperBag, Fig::StringUnravelEye, Fig::StringUnravelEyeBroken,
		Fig::BonVoyagingStar, Fig::BonVoyagingStarBroken
	};

	int count = 0;
	for (Fig id : all)
	{
		//fig() 内部会按需加载并缓存 QPixmap
		fig(id);
		++count;
	}

	//道具图标：如果原图已经预加载过（正常启动流程会），这里顺手把位图也建好；
	//如果这时道具数据还没加载，下面这个循环是空转，不会有副作用
	for (const QString& name : ItemDatabase::instance().itemNames())
	{
		itemIcon(name);
		++count;
	}

	FileManager::instance().log("fig", QString("位图缓存预热完成: %1 张").arg(count));
}
