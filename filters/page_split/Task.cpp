/*
    Scan Tailor - Interactive post-processing tool for scanned pages.
    Copyright (C) 2007-2008  Joseph Artsimovich <joseph_a@mail.ru>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "Task.h"
#include "TaskStatus.h"
#include "Filter.h"
#include "OptionsWidget.h"
#include "Settings.h"
#include "Rule.h"
#include "PageSplitFinder.h"
#include "PageLayout.h"
#include "Dependencies.h"
#include "Params.h"
#include "FilterData.h"
#include "ImageMetadata.h"
#include "Dpm.h"
#include "Dpi.h"
#include "AutoDetectedLayout.h"
#include "PageSequence.h"
#include "OrthogonalRotation.h"
#include "ImageTransformation.h"
#include "filters/deskew/Task.h"
#include "ImageView.h"
#include "FilterUiInterface.h"
#include "DebugImages.h"
#include <QImage>
#include <QObject>
#include <memory>

namespace page_split
{

using imageproc::BinaryThreshold;

class Task::UiUpdater : public FilterResult
{
public:
	UiUpdater(IntrusivePtr<Filter> const& filter,
		std::auto_ptr<DebugImages> dbg_img,
		QImage const& image, ImageTransformation const& xform,
		OptionsWidget::UiData const& ui_data);
	
	virtual void updateUI(FilterUiInterface* ui);
	
	virtual IntrusivePtr<AbstractFilter> filter() { return m_ptrFilter; }
private:
	IntrusivePtr<Filter> m_ptrFilter;
	std::auto_ptr<DebugImages> m_ptrDbg;
	QImage m_image;
	ImageTransformation m_xform;
	OptionsWidget::UiData m_uiData;
};


Task::Task(
	IntrusivePtr<Filter> const& filter,
	IntrusivePtr<Settings> const& settings,
	IntrusivePtr<PageSequence> const& page_sequence,
	IntrusivePtr<deskew::Task> const& next_task,
	ImageId const& image_id, bool debug)
:	m_ptrFilter(filter),
	m_ptrSettings(settings),
	m_ptrPageSequence(page_sequence),
	m_ptrNextTask(next_task),
	m_imageId(image_id)
{
	if (debug) {
		m_ptrDbg.reset(new DebugImages);
	}
}

Task::~Task()
{
}

FilterResultPtr
Task::process(TaskStatus const& status, FilterData const& data)
{
	status.throwIfCancelled();
	
	OrthogonalRotation const pre_rotation(data.xform().preRotation());
	OptionsWidget::UiData ui_data;
	
	Rule const rule(m_ptrSettings->getRuleFor(m_imageId));
	int num_logical_pages = 1;
	switch (rule.layoutType()) {
		case Rule::AUTO_DETECT: {
			ImageMetadata const metadata(
				data.image().size(), Dpm(data.image())
			);
			num_logical_pages = PageSequence::adviseNumberOfLogicalPages(
				metadata, pre_rotation
			);
			ui_data.setAutoDetectedLayout(
				num_logical_pages == 1 ? SINGLE_PAGE : TWO_PAGES
			);
			break;
		}
		case Rule::SINGLE_PAGE:
			num_logical_pages = true;
			break;
		case Rule::TWO_PAGES:
			num_logical_pages = false;
			break;
	}
	
	bool const single_page = (num_logical_pages == 1);
	Dependencies const deps(data.image(), pre_rotation, single_page);
	PageLayout layout;
	AutoManualMode mode = MODE_AUTO;
	
	ui_data.setDependencies(deps);
	
	std::auto_ptr<Params> params(m_ptrSettings->getPageParams(m_imageId));
	if (params.get()) {
		mode = params->mode();
		if (deps.matches(params->dependencies(), mode)) {
			layout = params->pageLayout();
		}
	}
	
	if (layout.isNull()) {
		layout = PageSplitFinder::findSplitLine(
			data.image(), pre_rotation, data.bwThreshold(),
			single_page, m_ptrDbg.get()
		);
		
		Params const new_params(layout, deps, mode);
		m_ptrSettings->setPageParams(m_imageId, new_params);
		
		status.throwIfCancelled();
	}
	
	ui_data.setPageLayout(layout);
	ui_data.setMode(mode);
	
	m_ptrPageSequence->setLogicalPagesInImage(m_imageId, layout.numSubPages());
	
	if (m_ptrNextTask) {
		return m_ptrNextTask->process(status, data, layout);
	} else {
		return FilterResultPtr(
			new UiUpdater(
				m_ptrFilter, m_ptrDbg, data.image(),
				data.xform(), ui_data
			)
		);
	}
}


/*============================ Task::UiUpdater =========================*/

Task::UiUpdater::UiUpdater(
	IntrusivePtr<Filter> const& filter,
	std::auto_ptr<DebugImages> dbg_img,
	QImage const& image, ImageTransformation const& xform,
	OptionsWidget::UiData const& ui_data)
:	m_ptrFilter(filter),
	m_ptrDbg(dbg_img),
	m_image(image),
	m_xform(xform),
	m_uiData(ui_data)
{
}

void
Task::UiUpdater::updateUI(FilterUiInterface* ui)
{
	// This function is executed from the GUI thread.
	
	OptionsWidget* const opt_widget = m_ptrFilter->optionsWidget();
	opt_widget->postUpdateUI(m_uiData);
	ui->setOptionsWidget(opt_widget);
	
	ImageView* view = new ImageView(m_image, m_xform, m_uiData.pageLayout());
	ui->setImageWidget(view, m_ptrDbg.get());
	
	QObject::connect(
		view, SIGNAL(manualPageLayoutSet(PageLayout const&)),
		opt_widget, SLOT(manualPageLayoutSet(PageLayout const&))
	);
}

} // namespace page_split