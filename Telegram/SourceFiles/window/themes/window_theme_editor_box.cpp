/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/themes/window_theme_editor_box.h"

#include "window/themes/window_theme.h"
#include "window/themes/window_theme_editor.h"
#include "window/themes/window_theme_preview.h"
#include "window/window_controller.h"
#include "boxes/confirm_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/image/image_prepare.h"
#include "ui/toast/toast.h"
#include "info/profile/info_profile_button.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "storage/localstorage.h"
#include "core/file_utilities.h"
#include "core/application.h"
#include "core/event_filter.h"
#include "lang/lang_keys.h"
#include "base/zlib_help.h"
#include "base/unixtime.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "data/data_cloud_themes.h"
#include "storage/file_upload.h"
#include "mainwindow.h"
#include "layout.h"
#include "apiwrap.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"
#include "styles/style_settings.h"
#include "styles/style_boxes.h"

#include <QtCore/QBuffer>

namespace Window {
namespace Theme {
namespace {

constexpr auto kRandomSlugSize = 16;
constexpr auto kMinSlugSize = 5;
constexpr auto kMaxSlugSize = 64;

enum class SaveErrorType {
	Other,
	Name,
	Link,
};

class BackgroundSelector : public Ui::RpWidget {
public:
	BackgroundSelector(
		QWidget *parent,
		const QImage &background,
		const ParsedTheme &parsed);

	[[nodiscard]] ParsedTheme result() const;

	int resizeGetHeight(int newWidth) override;

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void updateThumbnail();
	void chooseBackgroundFromFile();

	object_ptr<Ui::LinkButton> _chooseFromFile;
	object_ptr<Ui::Checkbox> _tileBackground;

	QImage _background;
	ParsedTheme _parsed;
	QString _imageText;
	int _thumbnailSize = 0;
	QPixmap _thumbnail;

};

template <size_t Size>
QByteArray qba(const char(&string)[Size]) {
	return QByteArray::fromRawData(string, Size - 1);
}

QByteArray qba(QLatin1String string) {
	return QByteArray::fromRawData(string.data(), string.size());
}

BackgroundSelector::BackgroundSelector(
	QWidget *parent,
	const QImage &background,
	const ParsedTheme &parsed)
: RpWidget(parent)
, _chooseFromFile(
	this,
	tr::lng_settings_bg_from_file(tr::now),
	st::boxLinkButton)
, _tileBackground(
	this,
	tr::lng_settings_bg_tile(tr::now),
	parsed.tiled,
	st::defaultBoxCheckbox)
, _background(background)
, _parsed(parsed) {
	_imageText = tr::lng_theme_editor_saved_to_jpg(
		tr::now,
		lt_size,
		formatSizeText(_parsed.background.size()));
	_chooseFromFile->setClickedCallback([=] { chooseBackgroundFromFile(); });

	_thumbnailSize = st::boxTextFont->height
		+ st::themesSmallSkip
		+ _chooseFromFile->heightNoMargins()
		+ st::themesSmallSkip
		+ _tileBackground->heightNoMargins();
	resize(width(), _thumbnailSize + st::themesSmallSkip);

	updateThumbnail();
}

void BackgroundSelector::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto left = _thumbnailSize + st::themesSmallSkip;

	p.setPen(st::boxTextFg);
	p.setFont(st::boxTextFont);
	p.drawTextLeft(left, 0, width(), _imageText);

	p.drawPixmapLeft(0, 0, width(), _thumbnail);
}

int BackgroundSelector::resizeGetHeight(int newWidth) {
	const auto left = _thumbnailSize + st::themesSmallSkip;
	_chooseFromFile->moveToLeft(left, st::boxTextFont->height + st::themesSmallSkip);
	_tileBackground->moveToLeft(left, st::boxTextFont->height + st::themesSmallSkip + _chooseFromFile->height() + st::themesSmallSkip);
	return height();
}

void BackgroundSelector::updateThumbnail() {
	const auto size = _thumbnailSize;
	auto back = QImage(
		QSize(size, size) * cIntRetinaFactor(),
		QImage::Format_ARGB32_Premultiplied);
	back.setDevicePixelRatio(cRetinaFactor());
	{
		Painter p(&back);
		PainterHighQualityEnabler hq(p);

		auto &pix = _background;
		int sx = (pix.width() > pix.height()) ? ((pix.width() - pix.height()) / 2) : 0;
		int sy = (pix.height() > pix.width()) ? ((pix.height() - pix.width()) / 2) : 0;
		int s = (pix.width() > pix.height()) ? pix.height() : pix.width();
		p.drawImage(QRect(0, 0, size, size), pix, QRect(sx, sy, s, s));
	}
	Images::prepareRound(back, ImageRoundRadius::Small);
	_thumbnail = App::pixmapFromImageInPlace(std::move(back));
	_thumbnail.setDevicePixelRatio(cRetinaFactor());
	update();
}

void BackgroundSelector::chooseBackgroundFromFile() {
	const auto callback = [=](const FileDialog::OpenResult &result) {
		auto content = result.remoteContent;
		if (!result.paths.isEmpty()) {
			QFile f(result.paths.front());
			if (f.open(QIODevice::ReadOnly)) {
				content = f.readAll();
				f.close();
			}
		}
		if (!content.isEmpty()) {
			auto format = QByteArray();
			auto image = App::readImage(content, &format);
			if (!image.isNull()
				&& (format == "jpeg"
					|| format == "jpg"
					|| format == "png")) {
				_background = image;
				_parsed.background = content;
				_parsed.isPng = (format == "png");
				const auto phrase = _parsed.isPng
					? tr::lng_theme_editor_read_from_png
					: tr::lng_theme_editor_read_from_jpg;
				_imageText = phrase(
					tr::now,
					lt_size,
					formatSizeText(_parsed.background.size()));
				_tileBackground->setChecked(false);
				updateThumbnail();
			}
		}
	};
	FileDialog::GetOpenPath(
		this,
		tr::lng_theme_editor_choose_image(tr::now),
		"Image files (*.jpeg *.jpg *.png)",
		crl::guard(this, callback));
}

ParsedTheme BackgroundSelector::result() const {
	auto result = _parsed;
	result.tiled = _tileBackground->checked();
	return result;
}

bool PaletteChanged(
		const QByteArray &editorPalette,
		const QByteArray &originalPalette,
		const Data::CloudTheme &cloud) {
	return originalPalette.isEmpty()
		|| (editorPalette != WriteCloudToText(cloud) + originalPalette);
}

void ImportFromFile(
		not_null<Main::Session*> session,
		not_null<QWidget*> parent) {
	const auto &imgExtensions = cImgExtensions();
	auto filters = QStringList(
		qsl("Theme files (*.tdesktop-theme *.tdesktop-palette)"));
	filters.push_back(FileDialog::AllFilesFilter());
	const auto callback = crl::guard(session, [=](
		const FileDialog::OpenResult &result) {
		const auto path = result.paths.isEmpty()
			? QString()
			: result.paths.front();
		if (!path.isEmpty()) {
			Window::Theme::Apply(path);
		}
	});
	FileDialog::GetOpenPath(
		parent.get(),
		tr::lng_choose_image(tr::now),
		filters.join(qsl(";;")),
		crl::guard(parent, callback));
}

[[nodiscard]] QString BytesToUTF8(QLatin1String string) {
	return QString::fromUtf8(string.data(), string.size());
}

// They're duplicated in window_theme.cpp:ChatBackground::ChatBackground.
[[nodiscard]] QByteArray ReplaceAdjustableColors(QByteArray data) {
	const auto &themeObject = Background()->themeObject();
	const auto &paper = Background()->paper();
	const auto usingDefaultTheme = themeObject.pathAbsolute.isEmpty();
	const auto usingThemeBackground = usingDefaultTheme
		? Data::IsDefaultWallPaper(paper)
		: Data::IsThemeWallPaper(paper);

	if (usingThemeBackground) {
		return data;
	}

	const auto adjustables = base::flat_map<QByteArray, style::color>{
		{ qba(qstr("msgServiceBg")), st::msgServiceBg },
		{ qba(qstr("msgServiceBgSelected")), st::msgServiceBgSelected },
		{ qba(qstr("historyScrollBg")), st::historyScrollBg },
		{ qba(qstr("historyScrollBgOver")), st::historyScrollBgOver },
		{ qba(qstr("historyScrollBarBg")), st::historyScrollBarBg },
		{ qba(qstr("historyScrollBarBgOver")), st::historyScrollBarBgOver }
	};
	for (const auto &[name, color] : adjustables) {
		data = ReplaceValueInPaletteContent(
			data,
			name,
			ColorHexString(color->c));
	}
	return data;
}

// Only is valid for current theme, pass Local::ReadThemeContent() here.
[[nodiscard]] ParsedTheme ParseTheme(
		const Object &theme,
		bool onlyPalette = false) {
	auto raw = ParsedTheme();
	raw.palette = theme.content;
	const auto result = [&] {
		if (const auto colorizer = ColorizerForTheme(theme.pathAbsolute)) {
			raw.palette = Editor::ColorizeInContent(
				std::move(raw.palette),
				colorizer);
		}
		raw.palette = ReplaceAdjustableColors(std::move(raw.palette));
		return raw;
	};

	zlib::FileToRead file(theme.content);

	unz_global_info globalInfo = { 0 };
	file.getGlobalInfo(&globalInfo);
	if (file.error() != UNZ_OK) {
		return result();
	}
	raw.palette = file.readFileContent("colors.tdesktop-theme", zlib::kCaseInsensitive, kThemeSchemeSizeLimit);
	if (file.error() == UNZ_END_OF_LIST_OF_FILE) {
		file.clearError();
		raw.palette = file.readFileContent("colors.tdesktop-palette", zlib::kCaseInsensitive, kThemeSchemeSizeLimit);
	}
	if (file.error() != UNZ_OK) {
		LOG(("Theme Error: could not read 'colors.tdesktop-theme' or 'colors.tdesktop-palette' in the theme file."));
		return ParsedTheme();
	} else if (onlyPalette) {
		return result();
	}

	const auto fromFile = [&](const char *filename) {
		raw.background = file.readFileContent(filename, zlib::kCaseInsensitive, kThemeBackgroundSizeLimit);
		if (file.error() == UNZ_OK) {
			return true;
		} else if (file.error() == UNZ_END_OF_LIST_OF_FILE) {
			file.clearError();
			return true;
		}
		LOG(("Theme Error: could not read '%1' in the theme file.").arg(filename));
		return false;
	};

	if (!fromFile("background.jpg") || !raw.background.isEmpty()) {
		return raw.background.isEmpty() ? ParsedTheme() : result();
	}
	raw.isPng = true;
	if (!fromFile("background.png") || !raw.background.isEmpty()) {
		return raw.background.isEmpty() ? ParsedTheme() : result();
	}
	raw.tiled = true;
	if (!fromFile("tiled.png") || !raw.background.isEmpty()) {
		return raw.background.isEmpty() ? ParsedTheme() : result();
	}
	raw.isPng = false;
	if (!fromFile("background.jpg") || !raw.background.isEmpty()) {
		return raw.background.isEmpty() ? ParsedTheme() : result();
	}
	return result();
}

QByteArray GenerateDefaultPalette() {
	auto result = QByteArray();
	const auto rows = style::main_palette::data();
	for (const auto &row : std::as_const(rows)) {
		result.append(qba(row.name)
		).append(": "
		).append(qba(row.value)
		).append("; // "
		).append(
			qba(
				row.description
			).replace(
				'\n',
				' '
			).replace(
				'\r',
				' ')
		).append('\n');
	}
	return result;
}

bool CopyColorsToPalette(
		const QString &path,
		const QByteArray &palette,
		const Data::CloudTheme &cloud) {
	QFile f(path);
	if (!f.open(QIODevice::WriteOnly)) {
		LOG(("Theme Error: could not open '%1' for writing.").arg(path));
		return false;
	}

	const auto prefix = WriteCloudToText(cloud);
	if (f.write(prefix) != prefix.size()
		|| f.write(palette) != palette.size()) {
		LOG(("Theme Error: could not write palette to '%1'").arg(path));
		return false;
	}
	return true;
}

[[nodiscard]] QString GenerateSlug() {
	const auto letters = uint8('Z' + 1 - 'A');
	const auto digits = uint8('9' + 1 - '0');
	const auto values = uint8(2 * letters + digits);

	auto result = QString();
	result.reserve(kRandomSlugSize);
	for (auto i = 0; i != kRandomSlugSize; ++i) {
		const auto value = rand_value<uint8>() % values;
		if (value < letters) {
			result.append(char('A' + value));
		} else if (value < 2 * letters) {
			result.append(char('a' + (value - letters)));
		} else {
			result.append(char('0' + (value - 2 * letters)));
		}
	}
	return result;
}

[[nodiscard]] QByteArray PackTheme(const ParsedTheme &parsed) {
	zlib::FileToWrite zip;

	zip_fileinfo zfi = { { 0, 0, 0, 0, 0, 0 }, 0, 0, 0 };
	const auto back = std::string(parsed.tiled ? "tiled" : "background")
		+ (parsed.isPng ? ".png" : ".jpg");
	zip.openNewFile(
		back.c_str(),
		&zfi,
		nullptr,
		0,
		nullptr,
		0,
		nullptr,
		Z_DEFLATED,
		Z_DEFAULT_COMPRESSION);
	zip.writeInFile(
		parsed.background.constData(),
		parsed.background.size());
	zip.closeFile();
	const auto scheme = "colors.tdesktop-theme";
	zip.openNewFile(
		scheme,
		&zfi,
		nullptr,
		0,
		nullptr,
		0,
		nullptr,
		Z_DEFLATED,
		Z_DEFAULT_COMPRESSION);
	zip.writeInFile(parsed.palette.constData(), parsed.palette.size());
	zip.closeFile();
	zip.close();

	if (zip.error() != ZIP_OK) {
		LOG(("Theme Error: could not export zip-ed theme, status: %1"
			).arg(zip.error()));
		return QByteArray();
	}
	return zip.result();
}

[[nodiscard]] bool IsGoodSlug(const QString &slug) {
	if (slug.size() < kMinSlugSize || slug.size() > kMaxSlugSize) {
		return false;
	}
	const auto i = ranges::find_if(slug, [](QChar ch) {
		return (ch < 'A' || ch > 'Z')
			&& (ch < 'a' || ch > 'z')
			&& (ch < '0' || ch > '9')
			&& (ch != '_');
	});
	return (i == slug.end());
}

SendMediaReady PrepareThemeMedia(
		const QString &name,
		const QByteArray &content) {
	PreparedPhotoThumbs thumbnails;
	QVector<MTPPhotoSize> sizes;

	auto thumbnail = GeneratePreview(content, QString()).scaled(
		320,
		320,
		Qt::KeepAspectRatio,
		Qt::SmoothTransformation);
	auto thumbnailBytes = QByteArray();
	{
		QBuffer buffer(&thumbnailBytes);
		thumbnail.save(&buffer, "JPG", 87);
	}

	const auto push = [&](const char *type, QImage &&image) {
		sizes.push_back(MTP_photoSize(
			MTP_string(type),
			MTP_fileLocationToBeDeprecated(MTP_long(0), MTP_int(0)),
			MTP_int(image.width()),
			MTP_int(image.height()), MTP_int(0)));
		thumbnails.emplace(type[0], std::move(image));
	};
	push("s", std::move(thumbnail));

	const auto filename = File::NameFromUserString(name)
		+ qsl(".tdesktop-theme");
	auto attributes = QVector<MTPDocumentAttribute>(
		1,
		MTP_documentAttributeFilename(MTP_string(filename)));
	const auto id = rand_value<DocumentId>();
	const auto document = MTP_document(
		MTP_flags(0),
		MTP_long(id),
		MTP_long(0),
		MTP_bytes(),
		MTP_int(base::unixtime::now()),
		MTP_string("application/x-tgtheme-tdesktop"),
		MTP_int(content.size()),
		MTP_vector<MTPPhotoSize>(sizes),
		MTP_int(MTP::maindc()),
		MTP_vector<MTPDocumentAttribute>(attributes));

	return SendMediaReady(
		SendMediaType::ThemeFile,
		QString(), // filepath
		filename,
		content.size(),
		content,
		id,
		0,
		QString(),
		PeerId(),
		MTP_photoEmpty(MTP_long(0)),
		thumbnails,
		document,
		thumbnailBytes,
		0);
}

Fn<void()> SavePreparedTheme(
		not_null<Window::Controller*> window,
		const ParsedTheme &parsed,
		const QByteArray &originalContent,
		const ParsedTheme &originalParsed,
		const Data::CloudTheme &fields,
		Fn<void()> done,
		Fn<void(SaveErrorType,QString)> fail) {
	Expects(window->account().sessionExists());

	using Storage::UploadedThumbDocument;
	struct State {
		FullMsgId id;
		bool generating = false;
		mtpRequestId requestId = 0;
		QByteArray themeContent;
		QString filename;
		rpl::lifetime lifetime;
	};

	const auto session = &window->account().session();
	const auto api = &session->api();
	const auto state = std::make_shared<State>();
	state->id = FullMsgId(
		0,
		session->data().nextLocalMessageId());

	const auto creating = !fields.id
		|| (fields.createdBy != session->userId());
	const auto oldDocumentId = creating ? 0 : fields.documentId;
	const auto changed = (parsed.background != originalParsed.background)
		|| (parsed.tiled != originalParsed.tiled)
		|| PaletteChanged(parsed.palette, originalParsed.palette, fields);

	const auto finish = [=](const MTPTheme &result) {
		Background()->clearEditingTheme(ClearEditing::KeepChanges);
		done();

		const auto cloud = result.match([&](const MTPDtheme &data) {
			const auto result = Data::CloudTheme::Parse(session, data);
			session->data().cloudThemes().savedFromEditor(result);
			return result;
		}, [&](const MTPDthemeDocumentNotModified &data) {
			LOG(("API Error: Unexpected themeDocumentNotModified."));
			return fields;
		});
		if (cloud.documentId && !state->themeContent.isEmpty()) {
			const auto document = session->data().document(cloud.documentId);
			document->setDataAndCache(state->themeContent);
		}
		KeepFromEditor(
			originalContent,
			originalParsed,
			cloud,
			state->themeContent,
			parsed);
	};

	const auto createTheme = [=](const MTPDocument &data) {
		const auto document = session->data().processDocument(data);
		state->requestId = api->request(MTPaccount_CreateTheme(
			MTP_string(fields.slug),
			MTP_string(fields.title),
			document->mtpInput()
		)).done([=](const MTPTheme &result) {
			finish(result);
		}).fail([=](const RPCError &error) {
			fail(SaveErrorType::Other, error.type());
		}).send();
	};

	const auto updateTheme = [=](const MTPDocument &data) {
		using Flag = MTPaccount_UpdateTheme::Flag;
		const auto document = session->data().processDocument(data);
		const auto flags = Flag::f_title
			| Flag::f_slug
			| (data.type() == mtpc_documentEmpty
				? Flag(0)
				: Flag::f_document);
		state->requestId = api->request(MTPaccount_UpdateTheme(
			MTP_flags(flags),
			MTP_string(Data::CloudThemes::Format()),
			MTP_inputTheme(MTP_long(fields.id), MTP_long(fields.accessHash)),
			MTP_string(fields.slug),
			MTP_string(fields.title),
			document->mtpInput()
		)).done([=](const MTPTheme &result) {
			finish(result);
		}).fail([=](const RPCError &error) {
			fail(SaveErrorType::Other, error.type());
		}).send();
	};

	const auto uploadTheme = [=](const UploadedThumbDocument &data) {
		state->requestId = api->request(MTPaccount_UploadTheme(
			MTP_flags(MTPaccount_UploadTheme::Flag::f_thumb),
			data.file,
			data.thumb,
			MTP_string(state->filename),
			MTP_string("application/x-tgtheme-tdesktop")
		)).done([=](const MTPDocument &result) {
			if (creating) {
				createTheme(result);
			} else {
				updateTheme(result);
			}
		}).fail([=](const RPCError &error) {
			fail(SaveErrorType::Other, error.type());
		}).send();
	};

	const auto uploadFile = [=](const QByteArray &theme) {
		const auto media = PrepareThemeMedia(fields.title, theme);
		state->filename = media.filename;
		state->themeContent = theme;

		session->uploader().thumbDocumentReady(
		) | rpl::filter([=](const UploadedThumbDocument &data) {
			return data.fullId == state->id;
		}) | rpl::start_with_next([=](const UploadedThumbDocument &data) {
			uploadTheme(data);
		}, state->lifetime);

		session->uploader().uploadMedia(state->id, media);
	};

	const auto save = [=] {
		if (!creating && !changed) {
			updateTheme(MTP_documentEmpty(MTP_long(fields.documentId)));
			return;
		}
		state->generating = true;
		crl::async([=] {
			crl::on_main([=, ready = PackTheme(parsed)]{
				if (!state->generating) {
					return;
				}
				state->generating = false;
				uploadFile(ready);
			});
		});
	};

	const auto checkFields = [=] {
		state->requestId = api->request(MTPaccount_CreateTheme(
			MTP_string(fields.slug),
			MTP_string(fields.title),
			MTP_inputDocumentEmpty()
		)).done([=](const MTPTheme &result) {
			save();
		}).fail([=](const RPCError &error) {
			if (error.type() == qstr("THEME_FILE_INVALID")) {
				save();
			} else {
				fail(SaveErrorType::Other, error.type());
			}
		}).send();
	};

	if (creating) {
		checkFields();
	} else {
		save();
	}

	return [=] {
		state->generating = false;
		api->request(base::take(state->requestId)).cancel();
		session->uploader().cancel(state->id);
		state->lifetime.destroy();
	};
}

} // namespace

bool PaletteChanged(
		const QByteArray &editorPalette,
		const Data::CloudTheme &cloud) {
	auto object = Local::ReadThemeContent();
	const auto real = object.content.isEmpty()
		? GenerateDefaultPalette()
		: ParseTheme(object, true).palette;
	return PaletteChanged(editorPalette, real, cloud);
}

void StartEditor(
		not_null<Window::Controller*> window,
		const Data::CloudTheme &cloud) {
	const auto path = EditingPalettePath();
	auto object = Local::ReadThemeContent();

	const auto palette = object.content.isEmpty()
		? GenerateDefaultPalette()
		: ParseTheme(object, true).palette;
	if (palette.isEmpty() || !CopyColorsToPalette(path, palette, cloud)) {
		window->show(Box<InformBox>(tr::lng_theme_editor_error(tr::now)));
		return;
	}
	Background()->setEditingTheme(cloud);
	window->showRightColumn(Box<Editor>(window, cloud));
}

void CreateBox(
		not_null<GenericBox*> box,
		not_null<Window::Controller*> window) {
	CreateForExistingBox(box, window, Data::CloudTheme());
}

void CreateForExistingBox(
		not_null<GenericBox*> box,
		not_null<Window::Controller*> window,
		const Data::CloudTheme &cloud) {
	const auto userId = window->account().sessionExists()
		? window->account().session().userId()
		: UserId(-1);
	const auto amCreator = window->account().sessionExists()
		&& (window->account().session().userId() == cloud.createdBy);
	box->setTitle(amCreator
		? (rpl::single(cloud.title) | Ui::Text::ToWithEntities())
		: tr::lng_theme_editor_create_title(Ui::Text::WithEntities));

	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		(amCreator
			? tr::lng_theme_editor_attach_description
			: tr::lng_theme_editor_create_description)(),
		st::boxDividerLabel));

	box->addRow(
		object_ptr<Info::Profile::Button>(
			box,
			tr::lng_theme_editor_import_existing() | Ui::Text::ToUpper(),
			st::createThemeImportButton),
		style::margins(
			0,
			st::boxRowPadding.left(),
			0,
			0)
	)->addClickHandler([=] {
		ImportFromFile(&window->account().session(), box);
	});

	const auto done = [=] {
		box->closeBox();
		StartEditor(window, cloud);
	};
	Core::InstallEventFilter(box, box, [=](not_null<QEvent*> event) {
		if (event->type() == QEvent::KeyPress) {
			const auto key = static_cast<QKeyEvent*>(event.get())->key();
			if (key == Qt::Key_Enter || key == Qt::Key_Return) {
				done();
				return Core::EventFilter::Result::Cancel;
			}
		}
		return Core::EventFilter::Result::Continue;
	});
	box->addButton(tr::lng_theme_editor_create(), done);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

void SaveTheme(
		not_null<Window::Controller*> window,
		const Data::CloudTheme &cloud,
		const QByteArray &palette,
		Fn<void()> unlock) {
	Expects(window->account().sessionExists());

	using Data::CloudTheme;

	const auto save = [=](const CloudTheme &fields) {
		unlock();
		window->show(Box(SaveThemeBox, window, fields, palette));
	};
	if (cloud.id) {
		window->account().session().api().request(MTPaccount_GetTheme(
			MTP_string(Data::CloudThemes::Format()),
			MTP_inputTheme(MTP_long(cloud.id), MTP_long(cloud.accessHash)),
			MTP_long(0)
		)).done([=](const MTPTheme &result) {
			result.match([&](const MTPDtheme &data) {
				save(CloudTheme::Parse(&window->account().session(), data));
			}, [&](const MTPDthemeDocumentNotModified &data) {
				LOG(("API Error: Unexpected themeDocumentNotModified."));
				save(CloudTheme());
			});
		}).fail([=](const RPCError &error) {
			save(CloudTheme());
		}).send();
	} else {
		save(CloudTheme());
	}
}

void SaveThemeBox(
		not_null<GenericBox*> box,
		not_null<Window::Controller*> window,
		const Data::CloudTheme &cloud,
		const QByteArray &palette) {
	Expects(window->account().sessionExists());

	const auto original = Local::ReadThemeContent();
	const auto originalContent = original.content;

	// We don't need default palette here, because in case of it we are
	// not interested if the palette was changed, we'll save it anyway.
	const auto originalParsed = originalContent.isEmpty()
		? ParsedTheme() // GenerateDefaultPalette()
		: ParseTheme(original);

	const auto background = Background()->createCurrentImage();
	const auto backgroundIsTiled = Background()->tile();
	const auto changed = !Data::IsThemeWallPaper(Background()->paper())
		|| originalParsed.background.isEmpty()
		|| ColorizerForTheme(original.pathAbsolute);

	auto parsed = ParsedTheme();
	parsed.palette = StripCloudTextFields(palette);
	parsed.isPng = false;
	if (changed) {
		QBuffer buffer(&parsed.background);
		background.save(&buffer, "JPG", 87);
	} else {
		// Use existing background serialization.
		parsed.background = originalParsed.background;
		parsed.isPng = originalParsed.isPng;
	}

	box->setTitle(tr::lng_theme_editor_save_title(Ui::Text::WithEntities));

	const auto name = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		tr::lng_theme_editor_name(),
		cloud.title));
	const auto linkWrap = box->addRow(
		object_ptr<Ui::RpWidget>(box),
		style::margins(
			st::boxRowPadding.left(),
			st::themesSmallSkip,
			st::boxRowPadding.right(),
			st::boxRowPadding.bottom()));
	const auto link = Ui::CreateChild<Ui::UsernameInput>(
		linkWrap,
		st::createThemeLink,
		rpl::single(qsl("link")),
		cloud.slug.isEmpty() ? GenerateSlug() : cloud.slug,
		true);
	linkWrap->widthValue(
	) | rpl::start_with_next([=](int width) {
		link->resize(width, link->height());
		link->moveToLeft(0, 0, width);
	}, link->lifetime());
	link->heightValue(
	) | rpl::start_with_next([=](int height) {
		linkWrap->resize(linkWrap->width(), height);
	}, link->lifetime());
	link->setLinkPlaceholder(
		Core::App().createInternalLink(qsl("addtheme/")));
	link->setPlaceholderHidden(false);
	link->setMaxLength(kMaxSlugSize);

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_theme_editor_link_about(),
			st::boxDividerLabel),
		style::margins(
			st::boxRowPadding.left(),
			st::themesSmallSkip,
			st::boxRowPadding.right(),
			st::boxRowPadding.bottom()));

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_theme_editor_background_image(),
			st::settingsSubsectionTitle),
		st::settingsSubsectionTitlePadding);
	const auto back = box->addRow(
		object_ptr<BackgroundSelector>(
			box,
			background,
			parsed),
		style::margins(
			st::boxRowPadding.left(),
			st::themesSmallSkip,
			st::boxRowPadding.right(),
			st::boxRowPadding.bottom()));

	box->setFocusCallback([=] { name->setFocusFast(); });

	box->setWidth(st::boxWideWidth);

	const auto saving = box->lifetime().make_state<bool>();
	const auto cancel = std::make_shared<Fn<void()>>(nullptr);
	box->lifetime().add([=] { if (*cancel) (*cancel)(); });
	const auto save = [=] {
		if (*saving) {
			return;
		}
		const auto done = crl::guard(box, [=] {
			box->closeBox();
			window->showRightColumn(nullptr);
		});
		const auto fail = crl::guard(box, [=](
				SaveErrorType type,
				const QString &error) {
			*saving = false;
			box->showLoading(false);
			if (error == qstr("THEME_TITLE_INVALID")) {
				type = SaveErrorType::Name;
			} else if (error == qstr("THEME_SLUG_INVALID")) {
				type = SaveErrorType::Link;
			} else if (error == qstr("THEME_SLUG_OCCUPIED")) {
				Ui::Toast::Show(
					tr::lng_create_channel_link_occupied(tr::now));
				type = SaveErrorType::Link;
			} else if (!error.isEmpty()) {
				Ui::Toast::Show(error);
			}
			if (type == SaveErrorType::Name) {
				name->showError();
			} else if (type == SaveErrorType::Link) {
				link->showError();
			}
		});
		auto fields = cloud;
		fields.title = name->getLastText().trimmed();
		fields.slug = link->getLastText().trimmed();
		if (fields.title.isEmpty()) {
			fail(SaveErrorType::Name, QString());
			return;
		} else if (!IsGoodSlug(fields.slug)) {
			fail(SaveErrorType::Link, QString());
			return;
		}

		*saving = true;
		box->showLoading(true);
		*cancel = SavePreparedTheme(
			window,
			back->result(),
			originalContent,
			originalParsed,
			fields,
			done,
			fail);
	};
	box->addButton(tr::lng_settings_save(), save);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

} // namespace Theme
} // namespace Window