#include "ExportSequenceViewItems.h"
#include "ExportUtils.h"
#include "ExportSequenceTask.h"
#include "ExportSequencesDialog.h"
#include "ExportAnnotationsDialog.h"
#include "ExportAnnotations2CSVTask.h"
#include "ExportSequences2MSADialog.h"
#include "GetSequenceByIdDialog.h"

#include <U2Core/AppContext.h>
#include <U2Core/DNATranslation.h>
#include <U2Core/BaseDocumentFormats.h>
#include <U2Core/GUrlUtils.h>
#include <U2Core/DocumentUtils.h>
#include <U2Core/L10n.h>

#include <U2Core/GObjectTypes.h>
#include <U2Core/AnnotationTableObject.h>
#include <U2Core/DNASequenceObject.h>
#include <U2Core/GObjectUtils.h>
#include <U2Core/MAlignmentObject.h>

#include <U2Core/GObjectSelection.h>
#include <U2Core/DocumentSelection.h>
#include <U2Core/SelectionUtils.h>
#include <U2Core/AnnotationSelection.h>
#include <U2Core/DNASequenceSelection.h>
#include <U2Core/GObjectRelationRoles.h>

#include <U2View/AnnotatedDNAView.h>
#include <U2View/ADVSequenceObjectContext.h>
#include <U2View/ADVConstants.h>


#include <U2Misc/DialogUtils.h>
#include <U2Gui/GUIUtils.h>
#include <U2Core/TextUtils.h>
#include <U2Core/LoadRemoteDocumentTask.h>
#include <U2Gui/OpenViewTask.h>

#include <QtGui/QMainWindow>
#include <QtGui/QMessageBox>

namespace U2 {


//////////////////////////////////////////////////////////////////////////
// ExportSequenceViewItemsController

ExportSequenceViewItemsController::ExportSequenceViewItemsController(QObject* p) 
: GObjectViewWindowContext(p, ANNOTATED_DNA_VIEW_FACTORY_ID)
{
}


void ExportSequenceViewItemsController::initViewContext(GObjectView* v) {
    AnnotatedDNAView* av = qobject_cast<AnnotatedDNAView*>(v);
    ADVExportContext* vc= new ADVExportContext(av);
    addViewResource(av, vc);
}


void ExportSequenceViewItemsController::buildMenu(GObjectView* v, QMenu* m) {
    QList<QObject*> resources = viewResources.value(v);
    assert(resources.size() == 1);
    QObject* r = resources.first();
    ADVExportContext* vc = qobject_cast<ADVExportContext*>(r);
    assert(vc!=NULL);
    vc->buildMenu(m);
}


//////////////////////////////////////////////////////////////////////////
// ADV view context


ADVExportContext::ADVExportContext(AnnotatedDNAView* v) : view(v) {
    sequence2SequenceAction = new QAction(tr("Export selected sequence region..."), this);
    connect(sequence2SequenceAction, SIGNAL(triggered()), SLOT(sl_saveSelectedSequences()));

    annotations2SequenceAction = new QAction(tr("Export sequence of selected annotations..."), this);
    connect(annotations2SequenceAction, SIGNAL(triggered()), SLOT(sl_saveSelectedAnnotationsSequence()));

    annotations2CSVAction = new QAction(tr("Export annotations..."), this);
    connect(annotations2CSVAction, SIGNAL(triggered()), SLOT(sl_saveSelectedAnnotations()));

    annotationsToAlignmentAction = new QAction(QIcon(":core/images/msa.png"), tr("Align selected annotations..."), this);
    connect(annotationsToAlignmentAction, SIGNAL(triggered()), SLOT(sl_saveSelectedAnnotationsToAlignment()));

    annotationsToAlignmentWithTranslatedAction = new QAction(QIcon(":core/images/msa.png"), tr("Align selected annotations (amino acids)..."), this);
    connect(annotationsToAlignmentWithTranslatedAction, SIGNAL(triggered()), SLOT(sl_saveSelectedAnnotationsToAlignmentWithTranslation()));

    sequenceToAlignmentAction = new QAction(QIcon(":core/images/msa.png"), tr("Align selected sequence regions..."), this);
    connect(sequenceToAlignmentAction, SIGNAL(triggered()), SLOT(sl_saveSelectedSequenceToAlignment()));

    sequenceToAlignmentWithTranslationAction = new QAction(QIcon(":core/images/msa.png"), tr("Align selected sequence regions (amino acids)..."), this);
    connect(sequenceToAlignmentWithTranslationAction, SIGNAL(triggered()), SLOT(sl_saveSelectedSequenceToAlignmentWithTranslation()));

    sequenceById = new QAction(tr("Export sequences by 'id'"), this);
    connect(sequenceById, SIGNAL(triggered()), SLOT(sl_getSequenceById()));
    sequenceByAccession = new QAction(tr("Export sequences by 'accession'"), this);
    connect(sequenceByAccession, SIGNAL(triggered()), SLOT(sl_getSequenceByAccession()));
    sequenceByDBXref = new QAction(tr("Export sequences by 'db_xref'"), this);
    connect(sequenceByDBXref, SIGNAL(triggered()), SLOT(sl_getSequenceByDBXref()));

    connect(view->getAnnotationsSelection(), 
        SIGNAL(si_selectionChanged(AnnotationSelection*, const QList<Annotation*>&, const QList<Annotation*>& )), 
        SLOT(sl_onAnnotationSelectionChanged(AnnotationSelection*, const QList<Annotation*>&, const QList<Annotation*>&)));

    connect(view, SIGNAL(si_sequenceAdded(ADVSequenceObjectContext*)), SLOT(sl_onSequenceContextAdded(ADVSequenceObjectContext*)));
    connect(view, SIGNAL(si_sequenceRemoved(ADVSequenceObjectContext*)), SLOT(sl_onSequenceContextRemoved(ADVSequenceObjectContext*)));
    foreach(ADVSequenceObjectContext* sCtx, view->getSequenceContexts()) {
        sl_onSequenceContextAdded(sCtx);
    }
}

void ADVExportContext::sl_onSequenceContextAdded(ADVSequenceObjectContext* c) {
    connect(c->getSequenceSelection(), 
        SIGNAL(si_selectionChanged(LRegionsSelection*, const QVector<U2Region>&, const QVector<U2Region>&)), 
        SLOT(sl_onSequenceSelectionChanged(LRegionsSelection*, const QVector<U2Region>&, const QVector<U2Region>&)));

    updateActions();
}

void ADVExportContext::sl_onSequenceContextRemoved(ADVSequenceObjectContext* c) {
    c->disconnect(this);
    updateActions();
}

void ADVExportContext::sl_onAnnotationSelectionChanged(AnnotationSelection* , const QList<Annotation*>& , const QList<Annotation*>&) {
    updateActions();
}

void ADVExportContext::sl_onSequenceSelectionChanged(LRegionsSelection* , const QVector<U2Region>& , const QVector<U2Region>& ) {
    updateActions();
}


static bool allNucleic(const QList<ADVSequenceObjectContext*>& seqs) {
    foreach(const ADVSequenceObjectContext* s, seqs) {
        if (!s->getAlphabet()->isNucleic()) {
            return false;
        }
    }
    return true;
}

void ADVExportContext::updateActions() {
    bool hasSelectedAnnotations = !view->getAnnotationsSelection()->isEmpty();
    bool hasSelectedGroups = view->getAnnotationsGroupSelection();
    int nSequenceSelections = 0;
    foreach(ADVSequenceObjectContext* c, view->getSequenceContexts()) {
        nSequenceSelections += c->getSequenceSelection()->getSelectedRegions().count();
    }

    sequence2SequenceAction->setEnabled(nSequenceSelections>=1);
    annotations2SequenceAction->setEnabled(hasSelectedAnnotations);
    annotations2CSVAction->setEnabled(hasSelectedAnnotations || hasSelectedGroups);

    bool _allNucleic = allNucleic(view->getSequenceContexts());

    bool hasMultipleAnnotationsSelected = view->getAnnotationsSelection()->getSelection().size() > 1;
    annotationsToAlignmentAction->setEnabled(hasMultipleAnnotationsSelected);
    annotationsToAlignmentWithTranslatedAction->setEnabled(hasMultipleAnnotationsSelected && _allNucleic);

    bool hasMultiSequenceSelection = nSequenceSelections > 1;
    sequenceToAlignmentAction->setEnabled(hasMultiSequenceSelection);
    sequenceToAlignmentWithTranslationAction->setEnabled(hasMultiSequenceSelection && _allNucleic);
}

void ADVExportContext::buildMenu(QMenu* m) {
    QMenu* alignMenu = GUIUtils::findSubMenu(m, ADV_MENU_ALIGN);
    alignMenu->addAction(sequenceToAlignmentAction);
    alignMenu->addAction(sequenceToAlignmentWithTranslationAction);
    alignMenu->addAction(annotationsToAlignmentAction);
    alignMenu->addAction(annotationsToAlignmentWithTranslatedAction);

    QMenu* exportMenu = GUIUtils::findSubMenu(m, ADV_MENU_EXPORT);
    exportMenu->addAction(sequence2SequenceAction);
    exportMenu->addAction(annotations2SequenceAction);
    exportMenu->addAction(annotations2CSVAction);

    bool isShowId = false;
    bool isShowAccession = false;
    bool isShowDBXref = false;
    QString name;
    if(!view->getAnnotationsSelection()->getSelection().isEmpty()) {
        name = view->getAnnotationsSelection()->getSelection().first().annotation->getAnnotationName();
    }
    foreach(const AnnotationSelectionData &sel, view->getAnnotationsSelection()->getSelection()) {
        if(name != sel.annotation->getAnnotationName()) {
            name = "";
        }
        
        if(!isShowId && !sel.annotation->findFirstQualifierValue("id").isEmpty()) {
            isShowId = true;
        } else if(!isShowAccession && !sel.annotation->findFirstQualifierValue("accession").isEmpty()) {
            isShowAccession = true;
        } else if(!isShowDBXref && !sel.annotation->findFirstQualifierValue("db_xref").isEmpty()) {
            isShowDBXref = true;
        }
    }
    if(isShowId || isShowAccession || isShowDBXref) {
        name = name.isEmpty() ? "" : "from '" + name + "'";
        QMenu *fetchMenu = new QMenu(tr("Fetch sequences from remote database"));
        m->insertMenu(exportMenu->menuAction(),fetchMenu);
        if(isShowId) {
            sequenceById->setText(tr("Fetch sequences by 'id' %1").arg(name));
            fetchMenu->addAction(sequenceById);
        }
        if(isShowAccession) {
            sequenceByAccession->setText(tr("Fetch sequences by 'accession' %1").arg(name));
            fetchMenu->addAction(sequenceByAccession);
        }
        if(isShowDBXref) {
            sequenceByDBXref->setText(tr("Fetch sequences by 'db_xref' %1").arg(name));
            fetchMenu->addAction(sequenceByDBXref);
        }
    }
}


void ADVExportContext::sl_saveSelectedAnnotationsSequence() {
    AnnotationSelection* as = view->getAnnotationsSelection();
    AnnotationGroupSelection* ags = view->getAnnotationsGroupSelection();

    QSet<Annotation*> annotations;
    const QList<AnnotationSelectionData>& aData = as->getSelection();
    foreach(const AnnotationSelectionData& ad, aData) {
        annotations.insert(ad.annotation);
    }

    const QList<AnnotationGroup*>& groups =  ags->getSelection();
    foreach(AnnotationGroup* g, groups) {
        g->findAllAnnotationsInGroupSubTree(annotations);
    }

    if (annotations.isEmpty()) {
        QMessageBox::warning(view->getWidget(), L10N::warningTitle(), tr("No annotations selected!"));
        return;
    }
    
    bool allowComplement = true;
    bool allowTranslation = true;
    bool allowBackTranslation = true;

    QMap<const ADVSequenceObjectContext*, QList<SharedAnnotationData> > annotationsPerSeq;
    foreach(Annotation* a, annotations) {
        ADVSequenceObjectContext* seqCtx = view->getSequenceContext(a->getGObject());
        if (seqCtx == NULL) {
            continue;
        }
        QList<SharedAnnotationData>& annsPerSeq = annotationsPerSeq[seqCtx];
        annsPerSeq.append(a->data());
        if (annsPerSeq.size() > 1) {
            continue;
        }
        DNASequenceObject* seqObj = seqCtx->getSequenceObject();
        if (GObjectUtils::findComplementTT(seqObj) == NULL) {
            allowComplement = false;
        }
        if (GObjectUtils::findAminoTT(seqObj, false) == NULL) {
            allowTranslation = false;
        }
        if (GObjectUtils::findBackTranslationTT(seqObj) == NULL) {
            allowBackTranslation = false;
        }
    }

    QString fileExt = AppContext::getDocumentFormatRegistry()->getFormatById(BaseDocumentFormats::PLAIN_FASTA)->getSupportedDocumentFileExtensions().first();
    GUrl seqUrl = view->getSequenceInFocus()->getSequenceGObject()->getDocument()->getURL();
    GUrl defaultUrl = GUrlUtils::rollFileName(seqUrl.dirPath() + "/" + seqUrl.baseFileName() + "_annotation." + fileExt, DocumentUtils::getNewDocFileNameExcludesHint());

    ExportSequencesDialog d(true, allowComplement, allowTranslation, allowBackTranslation, defaultUrl.getURLString(), BaseDocumentFormats::PLAIN_FASTA, AppContext::getMainWindow()->getQMainWindow());
    d.setWindowTitle(annotations2SequenceAction->text());
    d.disableAllFramesOption(true); // only 1 frame is suitable
    d.disableStrandOption(true);    // strand is already recorded in annotation
    d.disableAnnotationsOption(true);   // here we do not export annotations for sequence under another annotations
    int rc = d.exec();
    if (rc == QDialog::Rejected) {
        return;
    }
    assert(d.file.length() > 0);

    ExportAnnotationSequenceTaskSettings s;
    ExportUtils::loadDNAExportSettingsFromDlg(s.exportSequenceSettings,d);
    foreach(const ADVSequenceObjectContext* seqCtx, annotationsPerSeq.keys()) {
        ExportSequenceAItem ei;
        DNASequenceObject* seqObj = seqCtx->getSequenceObject();
        ei.sequence = seqObj->getDNASequence();
        ei.complTT = seqCtx->getComplementTT();
        ei.aminoTT = d.translate ? seqCtx->getAminoTT() : NULL;
        if (d.useSpecificTable && ei.sequence.alphabet->isNucleic()) {
            DNATranslationRegistry* tr = AppContext::getDNATranslationRegistry();
            ei.aminoTT = tr->lookupTranslation(seqObj->getAlphabet(), DNATranslationType_NUCL_2_AMINO, d.translationTable);
        }
        ei.annotations = annotationsPerSeq.value(seqCtx);
        s.items.append(ei);
    }
    Task* t = ExportUtils::wrapExportTask(new ExportAnnotationSequenceTask(s), d.addToProject);
    AppContext::getTaskScheduler()->registerTopLevelTask(t);
}

static QList<SharedAnnotationData> findAnnotationsInRegion(const U2Region& region, const QList<AnnotationTableObject*>& aobjects) {
    QList<SharedAnnotationData> result;
    foreach (const AnnotationTableObject* aobj, aobjects) {
        foreach(Annotation* a, aobj->getAnnotations()) {
            U2Region areg = U2Region::containingRegion(a->getRegions());
            if (region.contains(areg)) {
                result.append(a->data());
            }
        }
    }
    return result;
}

void ADVExportContext::sl_saveSelectedSequences() {
    ADVSequenceObjectContext* seqCtx = view->getSequenceInFocus();
    DNASequenceSelection* sel  = NULL;
    if (seqCtx!=NULL) {
        //TODO: support multi-export..
        sel = seqCtx->getSequenceSelection();
    }
    if (sel == NULL || sel->isEmpty()) {
        QMessageBox::warning(view->getWidget(), L10N::warningTitle(), tr("No sequence regions selected!"));
        return;
    }

    const QVector<U2Region>& regions =  sel->getSelectedRegions();
    bool merge = regions.size() > 1;
    bool complement = seqCtx->getComplementTT()!=NULL;
    bool amino = seqCtx->getAminoTT()!=NULL;
    bool nucleic = GObjectUtils::findBackTranslationTT(seqCtx->getSequenceObject())!=NULL;

    QString fileExt = AppContext::getDocumentFormatRegistry()->getFormatById(BaseDocumentFormats::PLAIN_FASTA)->getSupportedDocumentFileExtensions().first();
    GUrl seqUrl = seqCtx->getSequenceGObject()->getDocument()->getURL();
    GUrl defaultUrl = GUrlUtils::rollFileName(seqUrl.dirPath() + "/" + seqUrl.baseFileName() + "_region." + fileExt, DocumentUtils::getNewDocFileNameExcludesHint());

    ExportSequencesDialog d(merge, complement, amino, nucleic, defaultUrl.getURLString(), BaseDocumentFormats::PLAIN_FASTA, AppContext::getMainWindow()->getQMainWindow());
    d.setWindowTitle(sequence2SequenceAction->text());
    int rc = d.exec();
    if (rc == QDialog::Rejected) {
        return;
    }
    assert(d.file.length() > 0);

    const QByteArray& sequence = seqCtx->getSequenceData();
    DNAAlphabet* al = seqCtx->getAlphabet();

    ExportSequenceTaskSettings s;
    ExportUtils::loadDNAExportSettingsFromDlg(s,d);
    QSet<QString> usedNames;
    foreach(const U2Region& r, regions) {
        QString prefix = QString("region [%1 %2]").arg(QString::number(r.startPos+1)).arg(QString::number(r.endPos()));
        QString name = prefix;
        for (int i = 0; i < s.items.size(); ++i) {
            if (usedNames.contains(name)) {
                name = prefix + "|" + QString::number(i);
            }
        }
        usedNames.insert(name);
        ExportSequenceItem ei;
        QByteArray seq(sequence.constData() + r.startPos, r.length);
        ei.sequence = DNASequence(name, seq, al);
        ei.complTT = seqCtx->getComplementTT();
        ei.aminoTT = d.translate ? (d.useSpecificTable ? GObjectUtils::findAminoTT(seqCtx->getSequenceObject(), false, d.translationTable) : seqCtx->getAminoTT()) : NULL;
        ei.backTT = d.backTranslate ? GObjectUtils::findBackTranslationTT(seqCtx->getSequenceObject(), d.translationTable) : NULL;
        ei.annotations = findAnnotationsInRegion(r, seqCtx->getAnnotationObjects(true).toList());
        s.items.append(ei);
    }
    Task* t = ExportUtils::wrapExportTask(new ExportSequenceTask(s), d.addToProject);
    AppContext::getTaskScheduler()->registerTopLevelTask(t);
}

void ADVExportContext::sl_saveSelectedAnnotations() {
    // find annotations: selected annotations, selected groups
    QSet<Annotation *> annotationSet;
    AnnotationSelection* as = view->getAnnotationsSelection();
    foreach(const AnnotationSelectionData &data, as->getSelection()) {
        annotationSet.insert(data.annotation);
    }
    foreach(AnnotationGroup *group, view->getAnnotationsGroupSelection()->getSelection()) {
        group->findAllAnnotationsInGroupSubTree(annotationSet);
    }

    if (annotationSet.isEmpty()) {
        QMessageBox::warning(view->getWidget(), L10N::warningTitle(), tr("No annotations selected!"));
        return;
    }

    Annotation* first = *annotationSet.begin();
    Document* doc = first->getGObject()->getDocument();
    ADVSequenceObjectContext *sequenceContext = view->getSequenceInFocus();
    
    GUrl url;
    if (doc != NULL) {
        url = doc->getURL();
    } else if (sequenceContext != NULL) {
        url = sequenceContext->getSequenceGObject()->getDocument()->getURL();
    } else {
        url = GUrl("newfile");
    }
    
    QString fileName = GUrlUtils::rollFileName(url.dirPath() + "/" + url.baseFileName() + "_annotations.csv", 
        DocumentUtils::getNewDocFileNameExcludesHint());
    ExportAnnotationsDialog d(fileName, AppContext::getMainWindow()->getQMainWindow());
    d.setWindowTitle(annotations2CSVAction->text());
    
    if (QDialog::Accepted != d.exec()) {
        return;
    }
    
    //TODO: lock documents or use shared-data objects
    QList<Annotation *> annotationList = annotationSet.toList();
    qStableSort(annotationList.begin(), annotationList.end(), Annotation::annotationLessThan);
    
    // run task
    Task * t = NULL;
    if(d.fileFormat() == ExportAnnotationsDialog::CSV_FORMAT_ID) {
        t = new ExportAnnotations2CSVTask(annotationList, sequenceContext->getSequenceData(),
            sequenceContext->getComplementTT(), d.exportSequence(), d.filePath());
    } else {
        t = ExportUtils::saveAnnotationsTask(d.filePath(), d.fileFormat(), annotationList);
    }
    AppContext::getTaskScheduler()->registerTopLevelTask(t);
}

//////////////////////////////////////////////////////////////////////////
// alignment part

#define MAX_ALI_MODEL (10*1000*1000)

QString ADVExportContext::prepareMAFromAnnotations(MAlignment& ma, bool translate) {
    assert(ma.isEmpty());
    const QList<AnnotationSelectionData>& selection = view->getAnnotationsSelection()->getSelection();
    if (selection.size() < 2) {
        return tr("At least 2 annotations are required");        
    }
    // check that all sequences are present and have the same alphabets
    DNAAlphabet* al = NULL;
    DNATranslation* complTT = NULL;
    foreach(const AnnotationSelectionData& a, selection) {
        AnnotationTableObject* ao = a.annotation->getGObject();
        ADVSequenceObjectContext* seqCtx = view->getSequenceContext(ao);
        if (seqCtx == NULL) {
            return tr("No sequence object found");
        }
        if (al == NULL ) {
            al = seqCtx->getAlphabet();
            complTT = seqCtx->getComplementTT();
        } else {
            DNAAlphabet* al2 = seqCtx->getAlphabet();
            //BUG524: support alphabet reduction
            if (al->getType() != al2->getType()) {
                return tr("Different sequence alphabets");                
            } else if (al != al2) {
                al = al->getMap().count(true) >= al2->getMap().count(true) ? al : al2;
            }
        }
    }
    int maxLen = 0;
    ma.setAlphabet(al);
    QSet<QString> names;
    foreach(const AnnotationSelectionData& a, selection) {
        QString rowName = ExportUtils::genUniqueName(names, a.annotation->getAnnotationName());
        AnnotationTableObject* ao = a.annotation->getGObject();
        ADVSequenceObjectContext* seqCtx = view->getSequenceContext(ao);
        const QByteArray& sequence = seqCtx->getSequenceData();

        maxLen = qMax(maxLen, a.getSelectedRegionsLen());
        if (maxLen * ma.getNumRows() > MAX_ALI_MODEL) {
            return tr("Alignment is too large");
        }

        bool doComplement = a.annotation->getStrand().isCompementary();
        DNATranslation* aminoTT = translate ? seqCtx->getAminoTT() : NULL;
        QByteArray rowSequence;
        AnnotationSelection::getAnnotationSequence(rowSequence, a, MAlignment_GapChar, sequence,  doComplement? complTT : NULL, aminoTT);
        ma.addRow(MAlignmentRow(rowName, rowSequence));
        names.insert(rowName);
    }
    return "";
}

QString ADVExportContext::prepareMAFromSequences(MAlignment& ma, bool translate) {
    assert(ma.isEmpty());

    DNAAlphabet* al = translate ? AppContext::getDNAAlphabetRegistry()->findById(BaseDNAAlphabetIds::AMINO_DEFAULT()) : NULL;

    //derive alphabet
    int nItems = 0;
    bool forceTranslation = false;
    foreach(ADVSequenceObjectContext* c, view->getSequenceContexts()) {
        if (c->getSequenceSelection()->isEmpty()) {
            continue;
        }
        nItems += c->getSequenceSelection()->getSelectedRegions().count();
        DNAAlphabet* seqAl = c->getAlphabet();
        if (al == NULL) {
            al = seqAl;
        } else if (al != seqAl) {
            if (al->isNucleic() && seqAl->isAmino()) {
                forceTranslation = true;
                al = seqAl;
            } else if (al->isAmino() && seqAl->isNucleic()) {
                forceTranslation = true;
            } else {
                return tr("Can't derive alignment alphabet");
            }
        }
    }

    if (nItems < 2) { 
        return tr("At least 2 sequences required");        
    }

    //cache sequences
    QSet<QString> names;
    QList<MAlignmentRow> rows;
    qint64 maxLen = 0;
    foreach(ADVSequenceObjectContext* c, view->getSequenceContexts()) {
        if (c->getSequenceSelection()->isEmpty()) {
            continue;
        }
        DNAAlphabet* seqAl = c->getAlphabet();
        DNATranslation* aminoTT = ((translate || forceTranslation) && seqAl->isNucleic()) ? c->getAminoTT() : NULL;
        foreach(const U2Region& r, c->getSequenceSelection()->getSelectedRegions()) {
            const QByteArray& seq = c->getSequenceData();
            maxLen = qMax(maxLen, r.length);
            if (maxLen * rows.size() > MAX_ALI_MODEL) {
                return tr("Alignment is too large");
            }
            QByteArray mid = seq.mid(r.startPos, r.length);
            if (aminoTT!=NULL) {
                int len = aminoTT->translate(mid.data(), mid.size());
                mid.resize(len);
            }
            MAlignmentRow row(ExportUtils::genUniqueName(names, c->getSequenceGObject()->getGObjectName()), mid);
            names.insert(row.getName());
            rows.append(row);
        }
    }

    ma.setAlphabet(al);
    foreach(const MAlignmentRow& row, rows) {
        ma.addRow(row);
    }
    return "";
}



void ADVExportContext::selectionToAlignment(const QString& title, bool annotations, bool translate) {
    MAlignment ma(MA_OBJECT_NAME);
    QString err = annotations ? prepareMAFromAnnotations(ma, translate) : prepareMAFromSequences(ma, translate);
    if (!err.isEmpty()) {
        QMessageBox::critical(NULL, L10N::errorTitle(), err);
        return;
    }

    DocumentFormatConstraints c;
    c.addFlagToSupport(DocumentFormatFlag_SupportWriting);
    c.supportedObjectTypes += GObjectTypes::MULTIPLE_ALIGNMENT;

    ExportSequences2MSADialog d(view->getWidget());
    d.setWindowTitle(title);
    d.setOkButtonText(tr("Create alignment"));
    d.setFileLabelText(tr("Save alignment to file"));
    int rc = d.exec();
    if (rc != QDialog::Accepted) {
        return;
    }
    Task* t = ExportUtils::wrapExportTask(new ExportAlignmentTask(ma, d.url, d.format), d.addToProjectFlag);
    AppContext::getTaskScheduler()->registerTopLevelTask(t);
}


void ADVExportContext::sl_saveSelectedAnnotationsToAlignment() {
    selectionToAlignment(annotationsToAlignmentAction->text(), true, false);   
}

void ADVExportContext::sl_saveSelectedAnnotationsToAlignmentWithTranslation() {
    selectionToAlignment(annotationsToAlignmentAction->text(), true, true);   
}


void ADVExportContext::sl_saveSelectedSequenceToAlignment() {
    selectionToAlignment(sequenceToAlignmentAction->text(), false, false);   
}

void ADVExportContext::sl_saveSelectedSequenceToAlignmentWithTranslation() {
    selectionToAlignment(sequenceToAlignmentWithTranslationAction->text(), false, true);   
}

void ADVExportContext::sl_getSequenceByDBXref() {
    const QList<AnnotationSelectionData>& selection = view->getAnnotationsSelection()->getSelection();

    QStringList genbankID ;
    foreach(const AnnotationSelectionData &sel, selection) {
        Annotation* ann = sel.annotation;
        QString tmp  = ann->findFirstQualifierValue("db_xref");
        if(!tmp.isEmpty()) {
            genbankID  << tmp.split(":").last();
        }
    }
    QString listId = genbankID.join(",");
    fetchSequencesFromRemoteDB(listId);
}

void ADVExportContext::sl_getSequenceByAccession() {
    const QList<AnnotationSelectionData>& selection = view->getAnnotationsSelection()->getSelection();

    QStringList genbankID ;
    foreach(const AnnotationSelectionData &sel, selection) {
        Annotation* ann = sel.annotation;
        QString tmp  = ann->findFirstQualifierValue("accession");
        if(!tmp.isEmpty()) {
            genbankID << tmp;
        }
    }
    QString listId = genbankID.join(",");
    fetchSequencesFromRemoteDB(listId);
}

void ADVExportContext::sl_getSequenceById() {
    const QList<AnnotationSelectionData>& selection = view->getAnnotationsSelection()->getSelection();

    QStringList genbankID ;
    foreach(const AnnotationSelectionData &sel, selection) {
        Annotation* ann = sel.annotation;
        QString tmp = ann->findFirstQualifierValue("id");
        if(!tmp.isEmpty()) {
            int off = tmp.indexOf("|");
            int off1 = tmp.indexOf("|", off + 1);
            genbankID  << tmp.mid(off + 1, off1 - off - 1);
        }
    }
    QString listId = genbankID.join(",");
    fetchSequencesFromRemoteDB(listId);
}

void ADVExportContext::fetchSequencesFromRemoteDB(const QString & listId) {
    const QList<AnnotationSelectionData>& selection = view->getAnnotationsSelection()->getSelection();
    AnnotationTableObject *ao = selection.first().annotation->getGObject();
    DNAAlphabet* seqAl = view->getSequenceObjectsWithContexts().first()->getAlphabet();

    QString db;
    if(seqAl->getId() == BaseDNAAlphabetIds::NUCL_DNA_DEFAULT()) {
        db = "NCBI GenBank (DNA sequence)";
    } else if(seqAl->getId() == BaseDNAAlphabetIds::AMINO_DEFAULT()) {
        db = "NCBI protein sequence database";
    } else {
        return;
    }
    
    GetSequenceByIdDialog dlg(view->getWidget());
    if(dlg.exec() == QDialog::Accepted) {
        QString dir = dlg.getDirectory();
        Task *t;
        if(dlg.isAddToProject()) {
            t = new LoadRemoteDocumentAndOpenViewTask(listId, db, dir);
        } else {
            t = new LoadRemoteDocumentTask(listId, db, dir);
        }
        AppContext::getTaskScheduler()->registerTopLevelTask(t);
    }
}

} //namespace
