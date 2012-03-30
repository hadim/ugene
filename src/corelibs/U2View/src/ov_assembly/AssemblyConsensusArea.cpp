/**
 * UGENE - Integrated Bioinformatics Tools.
 * Copyright (C) 2008-2011 UniPro <ugene@unipro.ru>
 * http://ugene.unipro.ru
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include "AssemblyConsensusArea.h"
#include "AssemblyConsensusTask.h"
#include "AssemblyBrowser.h"
#include "ExportConsensusTask.h"
#include "ExportConsensusDialog.h"

#include <QtGui/QPainter>
#include <QtGui/QMouseEvent>

#include <U2Core/BaseDocumentFormats.h>
#include <U2Core/DocumentModel.h>
#include <U2Core/U2AssemblyUtils.h>
#include <U2Core/U2SafePoints.h>

#include <U2Algorithm/AssemblyConsensusAlgorithmRegistry.h>
#include <U2Algorithm/BuiltInAssemblyConsensusAlgorithms.h>

namespace U2 {

AssemblyConsensusArea::AssemblyConsensusArea(AssemblyBrowserUi * ui)
    : AssemblySequenceArea(ui, AssemblyConsensusAlgorithm::EMPTY_CHAR), consensusAlgorithmMenu(NULL), consensusAlgorithm(NULL), canceled(false)
{
    setToolTip(tr("Consensus sequence"));
    connect(&consensusTaskRunner, SIGNAL(si_finished()), SLOT(sl_consensusReady()));

    AssemblyConsensusAlgorithmRegistry * registry = AppContext::getAssemblyConsensusAlgorithmRegistry();
    QString defaultId = BuiltInAssemblyConsensusAlgorithms::DEFAULT_ALGO;
    AssemblyConsensusAlgorithmFactory * f = registry->getAlgorithmFactory(defaultId);
    SAFE_POINT(f != NULL, QString("consensus algorithm factory %1 not found").arg(defaultId),);
    consensusAlgorithm = QSharedPointer<AssemblyConsensusAlgorithm>(f->createAlgorithm());

    setDiffCellRenderer();

    createContextMenu();
}

void AssemblyConsensusArea::createContextMenu() {
    contextMenu = new QMenu(this);

    contextMenu->addMenu(getConsensusAlgorithmMenu());

    QAction * exportAction = contextMenu->addAction(tr("Export consensus..."));
    connect(exportAction, SIGNAL(triggered()), SLOT(sl_exportConsensus()));

    QAction * diffAction = contextMenu->addAction(tr("Show difference from reference"));
    diffAction->setCheckable(true);
    diffAction->setChecked(true);
    connect(diffAction, SIGNAL(triggered(bool)), SLOT(sl_drawDifferenceChanged(bool)));
}

bool AssemblyConsensusArea::canDrawSequence() {
    return !getModel()->isEmpty();
}

QByteArray AssemblyConsensusArea::getSequenceRegion(U2OpStatus &os) {
    Q_UNUSED(os);
    return lastResult.consensus;
}

// If required region is not fully included in cache, other positions are filled with AssemblyConsensusAlgorithm::EMPTY_CHAR
static ConsensusInfo getPart(ConsensusInfo cache, U2Region region) {
    ConsensusInfo result;
    result.region = region;
    result.algorithmId = cache.algorithmId;
    result.consensus = QByteArray(region.length, AssemblyConsensusAlgorithm::EMPTY_CHAR);
    if(!cache.region.isEmpty() && cache.region.intersects(region)) {
        U2Region intersection = cache.region.intersect(region);
        SAFE_POINT(!intersection.isEmpty(), "consensus cache: intersection cannot be empty, possible race condition?", result);

        int offsetInCache = intersection.startPos - cache.region.startPos;
        int offsetInResult = intersection.startPos - region.startPos;
        memcpy(result.consensus.data() + offsetInResult, cache.consensus.constData() + offsetInCache, intersection.length);
    }
    return result;
}

void AssemblyConsensusArea::launchConsensusCalculation() {
    if(areCellsVisible()) {
        U2Region visibleRegion = getVisibleRegion();

        previousRegion = visibleRegion;
        if(cache.region.contains(visibleRegion) && cache.algorithmId == consensusAlgorithm->getId()) {
            lastResult = getPart(cache, visibleRegion);
            consensusTaskRunner.cancel();
        } else {
            AssemblyConsensusTaskSettings settings;
            settings.region = visibleRegion;
            settings.model = getModel();
            settings.consensusAlgorithm = consensusAlgorithm;
            consensusTaskRunner.run(new AssemblyConsensusTask(settings));
        }
    }
    canceled = false;
    sl_redraw();
}

void AssemblyConsensusArea::sl_offsetsChanged() {
    if(areCellsVisible() && getVisibleRegion() != previousRegion) {
        launchConsensusCalculation();
    }
}

void AssemblyConsensusArea::sl_zoomPerformed() {
    launchConsensusCalculation();
}

void AssemblyConsensusArea::drawSequence(QPainter &p) {
    if(areCellsVisible()) {
        U2Region visibleRegion = getVisibleRegion();
        if(! consensusTaskRunner.isFinished() || canceled) {
            if(!cache.region.isEmpty() && cache.region.intersects(visibleRegion)) {
                // Draw a known part while others are still being calculated
                // To do it, temporarily substitute lastResult with values from cache, then return it back
                ConsensusInfo storedLastResult = lastResult;
                lastResult = getPart(cache, visibleRegion);
                AssemblySequenceArea::drawSequence(p);
                p.fillRect(rect(), QColor(0xff, 0xff, 0xff, 0x7f));
                lastResult = storedLastResult;
            }
            QString message = consensusTaskRunner.isFinished() ? tr("Consensus calculation canceled") : tr("Calculating consensus...");
            p.drawText(rect(), Qt::AlignCenter, message);
        } else if(lastResult.region == visibleRegion && lastResult.algorithmId == consensusAlgorithm->getId()) {
            AssemblySequenceArea::drawSequence(p);
        } else if(cache.region.contains(visibleRegion) && cache.algorithmId == consensusAlgorithm->getId()) {
            lastResult = getPart(cache, visibleRegion);
            AssemblySequenceArea::drawSequence(p);
        } else {
            launchConsensusCalculation();
        }
    }
}

QMenu * AssemblyConsensusArea::getConsensusAlgorithmMenu() {
    if(consensusAlgorithmMenu == NULL) {
        consensusAlgorithmMenu = new QMenu(tr("Consensus algorithm"));

        AssemblyConsensusAlgorithmRegistry * registry = AppContext::getAssemblyConsensusAlgorithmRegistry();
        QList<AssemblyConsensusAlgorithmFactory*> factories = registry->getAlgorithmFactories();

        foreach(AssemblyConsensusAlgorithmFactory *f, factories) {
            QAction * action = consensusAlgorithmMenu->addAction(f->getName());
            action->setCheckable(true);
            action->setChecked(f == consensusAlgorithm->getFactory());
            action->setData(f->getId());
            connect(consensusAlgorithmMenu, SIGNAL(triggered(QAction*)), SLOT(sl_consensusAlgorithmChanged(QAction*)));
        }
    }
    return consensusAlgorithmMenu;
}

void AssemblyConsensusArea::sl_consensusAlgorithmChanged(QAction * action) {
    QString id = action->data().toString();
    AssemblyConsensusAlgorithmRegistry * registry = AppContext::getAssemblyConsensusAlgorithmRegistry();
    AssemblyConsensusAlgorithmFactory * f = registry->getAlgorithmFactory(id);
    SAFE_POINT(f != NULL, QString("cannot change consensus algorithm, invalid id %1").arg(id),);

    consensusAlgorithm = QSharedPointer<AssemblyConsensusAlgorithm>(f->createAlgorithm());

    foreach(QAction* a, consensusAlgorithmMenu->actions()) {
        a->setChecked(a == action);
    }

    launchConsensusCalculation();
}

void AssemblyConsensusArea::sl_drawDifferenceChanged(bool drawDifference) {
    if(drawDifference) {
        setDiffCellRenderer();
    } else {
        setNormalCellRenderer();
    }
    sl_redraw();
}

void AssemblyConsensusArea::mousePressEvent(QMouseEvent *e) {
    if(e->button() == Qt::RightButton) {
        contextMenu->exec(QCursor::pos());
    }
}

void AssemblyConsensusArea::sl_consensusReady() {
    if(consensusTaskRunner.isFinished()) {
        if(consensusTaskRunner.isSuccessful()) {
            cache = lastResult = consensusTaskRunner.getResult();
            canceled = false;
        } else {
            canceled = true;
        }
        sl_redraw();
    }
}

void AssemblyConsensusArea::sl_exportConsensus() {
    const DocumentFormat * defaultFormat = BaseDocumentFormats::get(BaseDocumentFormats::FASTA);
    SAFE_POINT(defaultFormat != NULL, "Internal: couldn't find default document format for consensus",);

    ExportConsensusTaskSettings settings;
    settings.region = getModel()->getGlobalRegion();
    settings.model = getModel();
    settings.consensusAlgorithm = consensusAlgorithm;
    settings.formatId = defaultFormat->getFormatId();
    settings.seqObjName = getModel()->getAssembly().visualName + "_consensus";
    settings.addToProject = true;
    settings.keepGaps = true;
    settings.circular = false;

    QFileInfo db(getModel()->getAssembly().dbiId);
    QString ext = defaultFormat->getSupportedDocumentFileExtensions().first();
    settings.fileName = QString("%1/%2_consensus.%3").arg(db.path()).arg(db.baseName()).arg(ext);

    ExportConsensusDialog dlg(this, settings, getVisibleRegion());
    if(dlg.exec() == QDialog::Accepted) {
        settings = dlg.getSettings();
        AppContext::getTaskScheduler()->registerTopLevelTask(new ExportConsensusTask(settings));
    }
}

} //ns
