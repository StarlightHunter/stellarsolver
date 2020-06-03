/*  SexySolver, SexySolver Internal Library developed by Robert Lancaster, 2020

    This application is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.
*/
#if defined(__APPLE__)
#include <sys/sysctl.h>
#elif defined(_WIN32)
#include "windows.h"
#else //Linux
#include <QProcess>
#endif

#include "sexysolver.h"
#include "sextractorsolver.h"
#include "externalsextractorsolver.h"
#include "onlinesolver.h"
#include <QApplication>


SexySolver::SexySolver(ProcessType type, Statistic imagestats, const uint8_t *imageBuffer, QObject *parent) : QThread(parent)
{
     processType = type;
     stats=imagestats;
     m_ImageBuffer=imageBuffer;
     subframe = QRect(0,0,stats.width,stats.height);
}

SexySolver::SexySolver(Statistic imagestats, uint8_t const *imageBuffer, QObject *parent) : QThread(parent)
{
     stats=imagestats;
     m_ImageBuffer=imageBuffer;
     subframe = QRect(0,0,stats.width,stats.height);
}

SexySolver::~SexySolver()
{

}

SextractorSolver* SexySolver::createSextractorSolver()
{
    SextractorSolver *solver;
    if(processType == INT_SEP || processType == INT_SEP_HFR || processType == SEXYSOLVER)
        solver = new InternalSextractorSolver(processType, stats, m_ImageBuffer, this);
    else if(processType == ONLINE_ASTROMETRY_NET || processType == INT_SEP_ONLINE_ASTROMETRY_NET)
    {
        OnlineSolver *onlineSolver = new OnlineSolver(processType, stats, m_ImageBuffer, this);
        onlineSolver->fileToProcess = fileToProcess;
        onlineSolver->astrometryAPIKey = astrometryAPIKey;
        onlineSolver->astrometryAPIURL = astrometryAPIURL;
        solver = onlineSolver;
    }
    else
    {
        ExternalSextractorSolver *extSolver = new ExternalSextractorSolver(processType, stats, m_ImageBuffer, this);
        extSolver->fileToProcess = fileToProcess;
        extSolver->sextractorBinaryPath = sextractorBinaryPath;
        extSolver->confPath = confPath;
        extSolver->solverPath = solverPath;
        extSolver->astapBinaryPath = astapBinaryPath;
        extSolver->wcsPath = wcsPath;
        extSolver->cleanupTemporaryFiles = cleanupTemporaryFiles;
        extSolver->autoGenerateAstroConfig = autoGenerateAstroConfig;
        solver = extSolver;
    }

    if(useSubframe)
        solver->setUseSubframe(subframe);
    solver->logToFile = logToFile;
    solver->logFileName = logFileName;
    solver->logLevel = logLevel;
    solver->basePath = basePath;
    solver->params = params;
    solver->indexFolderPaths = indexFolderPaths;
    if(use_scale)
        solver->setSearchScale(scalelo, scalehi, scaleunit);
    if(use_position)
        solver->setSearchPositionInDegrees(search_ra, search_dec);
    if(logLevel != LOG_NONE)
        connect(solver, &SextractorSolver::logOutput, this, &SexySolver::logOutput);

    return solver;
}


void SexySolver::sextract()
{
    processType = INT_SEP;
    useSubframe = false;
    executeProcess();
}

void SexySolver::sextractWithHFR()
{
    processType = INT_SEP_HFR;
    useSubframe = false;
    executeProcess();
}

void SexySolver::sextract(QRect frame)
{
    processType = INT_SEP;
    subframe = frame;
    useSubframe = true;
    executeProcess();
}

void SexySolver::sextractWithHFR(QRect frame)
{
    processType = INT_SEP_HFR;
    subframe = frame;
    useSubframe = true;
    executeProcess();
}

void SexySolver::startsextraction()
{
    processType = INT_SEP;
    startProcess();
}

void SexySolver::startSextractionWithHFR()
{
    processType = INT_SEP_HFR;
    startProcess();
}

void SexySolver::startProcess()
{
    if(checkParameters())
    {
        sextractorSolver = createSextractorSolver();
        start();
    }
}

void SexySolver::executeProcess()
{
    if(checkParameters())
    {
        sextractorSolver = createSextractorSolver();
        start();
        while(!hasSextracted && !hasSolved && !hasFailed && !wasAborted)
            QApplication::processEvents();
    }
}

bool SexySolver::checkParameters()
{
    if(params.multiAlgorithm == MULTI_AUTO)
    {
        if(use_scale && use_position)
            params.multiAlgorithm = NOT_MULTI;
        else if(use_position)
            params.multiAlgorithm = MULTI_SCALES;
        else if(use_scale)
            params.multiAlgorithm = MULTI_DEPTHS;
        else
            params.multiAlgorithm = MULTI_SCALES;
    }

    if(params.inParallel)
    {
        if(enoughRAMisAvailableFor(indexFolderPaths))
        {
            if(logLevel != LOG_NONE)
                emit logOutput("There should be enough RAM to load the indexes in parallel.");
        }
        else
        {
            if(logLevel != LOG_NONE)
                emit logOutput("Not enough RAM is available on this system for loading the index files you have in parallel");
            if(logLevel != LOG_NONE)
                emit logOutput("Disabling the inParallel option.");
            params.inParallel = false;
        }
    }
    return true; //For now
}

void SexySolver::run()
{
    hasFailed = false;
    if(processType == INT_SEP || processType == INT_SEP_HFR || processType == EXT_SEXTRACTOR || processType == EXT_SEXTRACTOR_HFR)
        hasSextracted = false;
    else
        hasSolved = false;

    //These are the ones that support parallelization
    if(params.multiAlgorithm != NOT_MULTI && (processType == SEXYSOLVER || processType == EXT_SEXTRACTORSOLVER || processType == INT_SEP_EXT_SOLVER))
    {
        sextractorSolver->sextract();
        parallelSolve();

        while(!hasSolved && !wasAborted && parallelSolversAreRunning())
            msleep(100);

        if(loadWCS && hasWCS && solverWithWCS)
        {
            wcs_coord = solverWithWCS->getWCSCoord();
            if(wcs_coord)
            {
                if(stars.count() > 0)
                    stars = solverWithWCS->appendStarsRAandDEC(stars);
                emit wcsDataisReady();
            }
        }
        while(parallelSolversAreRunning())
            msleep(100);
    }
    else if(processType == ONLINE_ASTROMETRY_NET || processType ==INT_SEP_ONLINE_ASTROMETRY_NET)
    {
        connect(sextractorSolver, &SextractorSolver::finished, this, &SexySolver::processFinished);
        sextractorSolver->startProcess();
        while(sextractorSolver->isRunning())
            msleep(100);
    }
    else
    {
        connect(sextractorSolver, &SextractorSolver::finished, this, &SexySolver::processFinished);
        sextractorSolver->executeProcess();
    }
    if(logLevel != LOG_NONE)
        emit logOutput("All Processes Complete");
}

//This allows us to start multiple threads to search simulaneously in separate threads/cores
//to attempt to efficiently use modern multi core computers to speed up the solve
void SexySolver::parallelSolve()
{
    if(params.multiAlgorithm == NOT_MULTI || !(processType == SEXYSOLVER || processType == EXT_SEXTRACTORSOLVER || processType == INT_SEP_EXT_SOLVER))
        return;
    parallelSolvers.clear();
    parallelFails = 0;
    int threads = idealThreadCount();

    if(params.multiAlgorithm == MULTI_SCALES)
    {
        //Attempt to search on multiple scales
        //Note, originally I had each parallel solver getting equal ranges, but solves are faster on bigger scales
        //So now I'm giving the bigger scale solvers more of a range to work with.
        double minScale;
        double maxScale;
        ScaleUnits units;
        if(use_scale)
        {
            minScale = scalelo;
            maxScale = scalehi;
            units = scaleunit;
        }
        else
        {
            minScale = params.minwidth;
            maxScale = params.maxwidth;
            units = DEG_WIDTH;
        }
        double scaleConst = (maxScale - minScale) / pow(threads,2);
        if(logLevel != LOG_NONE)
            emit logOutput(QString("Starting %1 threads to solve on multiple scales").arg(threads));
        for(double thread = 0; thread < threads; thread++)
        {
            double low = minScale + scaleConst * pow(thread,2);
            double high = minScale + scaleConst * pow(thread + 1, 2);
            SextractorSolver *solver = sextractorSolver->spawnChildSolver(thread);
            connect(solver, &SextractorSolver::finished, this, &SexySolver::finishParallelSolve);
            solver->setSearchScale(low, high, units);
            parallelSolvers.append(solver);
            if(logLevel != LOG_NONE)
                emit logOutput(QString("Solver # %1, Low %2, High %3 %4").arg(parallelSolvers.count()).arg(low).arg(high).arg(getScaleUnitString()));
        }
    }
    //Note: it might be useful to do a parallel solve on multiple positions, but I am afraid
    //that since it searches in a circle around the search position, it might be difficult to make it
    //search a square grid without either missing sections of sky or overlapping search regions.
    else if(params.multiAlgorithm == MULTI_DEPTHS)
    {
        //Attempt to search on multiple depths
        int sourceNum = 200;
        if(params.keepNum !=0)
            sourceNum = params.keepNum;
        int inc = sourceNum / threads;
        //We don't need an unnecessary number of threads
        if(inc < 10)
            inc = 10;
        if(logLevel != LOG_NONE)
            emit logOutput(QString("Starting %1 threads to solve on multiple depths").arg(sourceNum / inc));
        for(int i = 1; i < sourceNum; i += inc)
        {
            SextractorSolver *solver = sextractorSolver->spawnChildSolver(i);
            connect(solver, &SextractorSolver::finished, this, &SexySolver::finishParallelSolve);
            solver->depthlo = i;
            solver->depthhi = i + inc;
            parallelSolvers.append(solver);
            if(logLevel != LOG_NONE)
                emit logOutput(QString("Child Solver # %1, Depth Low %2, Depth High %3").arg(parallelSolvers.count()).arg(i).arg(i + inc));
        }
    }
    foreach(SextractorSolver *solver, parallelSolvers)
        solver->startProcess();
}

bool SexySolver::parallelSolversAreRunning()
{
    foreach(SextractorSolver *solver, parallelSolvers)
        if(solver->isRunning())
            return true;
    return false;
}
void SexySolver::processFinished(int code)
{
    numStars  = sextractorSolver->getNumStarsFound();
    if(code == 0)
    {
        //This means it was a Solving Command
        if(sextractorSolver->solvingDone())
        {
            solution = sextractorSolver->getSolution();
            if(sextractorSolver->hasWCSData())
            {
                hasWCS = true;
                solverWithWCS = sextractorSolver;
            }
            hasSolved = true;
        }
        //This means it was a Sextraction Command
        else
        {
            stars = sextractorSolver->getStarList();
            background = sextractorSolver->getBackground();
            calculateHFR = sextractorSolver->isCalculatingHFR();
            if(solverWithWCS)
                stars = solverWithWCS->appendStarsRAandDEC(stars);
            hasSextracted = true;
        }
    }
    else
        hasFailed = true;
    emit finished(code);
    if(sextractorSolver->solvingDone())
    {
        if(loadWCS && hasWCS && solverWithWCS)
        {
            wcs_coord = solverWithWCS->getWCSCoord();
            stars = solverWithWCS->appendStarsRAandDEC(stars);
            if(wcs_coord)
                emit wcsDataisReady();
        }
    }
}


//This slot listens for signals from the child solvers that they are in fact done with the solve
//If they
void SexySolver::finishParallelSolve(int success)
{
    SextractorSolver *reportingSolver = (SextractorSolver*)sender();
    int whichSolver = 0;
    for(int i =0; i<parallelSolvers.count(); i++ )
    {
        SextractorSolver *solver = parallelSolvers.at(i);
        if(solver == reportingSolver)
            whichSolver = i + 1;
    }

    if(success == 0)
    {
        numStars  = reportingSolver->getNumStarsFound();
        if(logLevel != LOG_NONE)
            emit logOutput(QString("Successfully solved with child solver: %1").arg(whichSolver));
        if(logLevel != LOG_NONE)
            emit logOutput("Shutting down other child solvers");
        foreach(SextractorSolver *solver, parallelSolvers)
        {
            disconnect(solver, &SextractorSolver::finished, this, &SexySolver::finishParallelSolve);
            disconnect(solver, &SextractorSolver::logOutput, this, &SexySolver::logOutput);
            if(solver != reportingSolver && solver->isRunning())
                solver->abort();
        }
        solution = reportingSolver->getSolution();
        if(reportingSolver->hasWCSData())
        {
            solverWithWCS = reportingSolver;
            hasWCS = true;
        }
        hasSolved = true;
        emit finished(0);
    }
    else
    {
        parallelFails++;
        if(logLevel != LOG_NONE)
            emit logOutput(QString("Child solver: %1 did not solve or was aborted").arg(whichSolver));
        if(parallelFails == parallelSolvers.count())
            emit finished(-1);
    }
}

//This is the abort method.  The way that it works is that it creates a file.  Astrometry.net is monitoring for this file's creation in order to abort.
void SexySolver::abort()
{
    foreach(SextractorSolver *solver, parallelSolvers)
        solver->abort();
    if(sextractorSolver)
        sextractorSolver->abort();
    wasAborted = true;
}

//This method uses a fwhm value to generate the conv filter the sextractor will use.
void SexySolver::createConvFilterFromFWHM(Parameters *params, double fwhm)
{
    params->fwhm = fwhm;
    params->convFilter.clear();
    double a = 1;
    int size = abs(ceil(fwhm * 0.6));
    for(int y = -size; y <= size; y++ )
    {
        for(int x = -size; x <= size; x++ )
        {
            double value = a * exp( ( -4.0 * log(2.0) * pow(sqrt( pow(x,2) + pow(y,2) ),2) ) / pow(fwhm,2));
            params->convFilter.append(value);
        }
    }
}

QList<Parameters> SexySolver::getBuiltInProfiles()
{
    QList<Parameters> profileList;

    Parameters fastSolving;
    fastSolving.listName = "FastSolving";
    fastSolving.downsample = 2;
    fastSolving.minwidth = 1;
    fastSolving.maxwidth = 10;
    fastSolving.keepNum = 50;
    fastSolving.maxEllipse = 1.5;
    createConvFilterFromFWHM(&fastSolving, 4);
    profileList.append(fastSolving);

    Parameters parSolving;
    parSolving.listName = "ParallelSolving";
    parSolving.multiAlgorithm = MULTI_AUTO;
    parSolving.downsample = 2;
    parSolving.minwidth = 1;
    parSolving.maxwidth = 10;
    parSolving.keepNum = 50;
    parSolving.maxEllipse = 1.5;
    createConvFilterFromFWHM(&parSolving, 2);
    profileList.append(parSolving);

    Parameters parLargeSolving;
    parLargeSolving.listName = "ParallelLargeScale";
    parLargeSolving.multiAlgorithm = MULTI_AUTO;
    parLargeSolving.downsample = 2;
    parLargeSolving.minwidth = 1;
    parLargeSolving.maxwidth = 10;
    parLargeSolving.keepNum = 50;
    parLargeSolving.maxEllipse = 1.5;
    createConvFilterFromFWHM(&parLargeSolving, 2);
    profileList.append(parLargeSolving);

    Parameters fastSmallSolving;
    fastSmallSolving.listName = "ParallelSmallScale";
    fastSmallSolving.multiAlgorithm = MULTI_AUTO;
    fastSmallSolving.downsample = 2;
    parLargeSolving.minwidth = 1;
    fastSmallSolving.maxwidth = 10;
    fastSmallSolving.keepNum = 50;
    fastSmallSolving.maxEllipse = 1.5;
    createConvFilterFromFWHM(&fastSmallSolving, 2);
    profileList.append(fastSmallSolving);

    Parameters stars;
    stars.listName = "AllStars";
    stars.maxEllipse = 1.5;
    createConvFilterFromFWHM(&stars, 1);
    stars.r_min = 2;
    profileList.append(stars);

    Parameters smallStars;
    smallStars.listName = "SmallSizedStars";
    smallStars.maxEllipse = 1.5;
    createConvFilterFromFWHM(&smallStars, 1);
    smallStars.r_min = 2;
    smallStars.maxSize = 5;
    smallStars.saturationLimit = 80;
    profileList.append(smallStars);

    Parameters mid;
    mid.listName = "MidSizedStars";
    mid.maxEllipse = 1.5;
    mid.minarea = 20;
    createConvFilterFromFWHM(&mid, 4);
    mid.r_min = 5;
    mid.removeDimmest = 20;
    mid.minSize = 2;
    mid.maxSize = 10;
    mid.saturationLimit = 80;
    profileList.append(mid);

    Parameters big;
    big.listName = "BigSizedStars";
    big.maxEllipse = 1.5;
    big.minarea = 40;
    createConvFilterFromFWHM(&big, 8);
    big.r_min = 20;
    big.minSize = 5;
    big.removeDimmest = 50;
    profileList.append(big);

    return profileList;
}

void SexySolver::setParameterProfile(ParametersProfile profile)
{
    QList<Parameters> profileList = getBuiltInProfiles();
    setParameters(profileList.at(profile));
}

void SexySolver::setUseSubframe(QRect frame)
{
    int x = frame.x();
    int y = frame.y();
    int w = frame.width();
    int h = frame.height();
    if(w < 0)
    {
        x = x + w; //It's negative
        w = -w;
    }
    if(h < 0)
    {
        y = y + h; //It's negative
        h = -h;
    }
    if(x < 0)
        x = 0;
    if(y < 0)
        y = 0;
    if(x > stats.width)
        x = stats.width;
    if(y > stats.height)
        y = stats.height;

    useSubframe = true;
    subframe = QRect(x, y, w, h);
}

//This is a convenience function used to set all the scale parameters based on the FOV high and low values wit their units.
void SexySolver::setSearchScale(double fov_low, double fov_high, QString scaleUnits)
{
    if(scaleUnits =="dw" || scaleUnits =="degw" || scaleUnits =="degwidth")
        setSearchScale(fov_low, fov_high, DEG_WIDTH);
    if(scaleUnits == "app" || scaleUnits == "arcsecperpix")
        setSearchScale(fov_low, fov_high, ARCSEC_PER_PIX);
    if(scaleUnits =="aw" || scaleUnits =="amw" || scaleUnits =="arcminwidth")
        setSearchScale(fov_low, fov_high, ARCMIN_WIDTH);
    if(scaleUnits =="focalmm")
        setSearchScale(fov_low, fov_high, FOCAL_MM);
}

//This is a convenience function used to set all the scale parameters based on the FOV high and low values wit their units.
void SexySolver::setSearchScale(double fov_low, double fov_high, ScaleUnits units)
{
    use_scale = true;
    //L
    scalelo = fov_low;
    //H
    scalehi = fov_high;
    //u
    scaleunit = units;
}

//This is a convenience function used to set all the search position parameters based on the ra, dec, and radius
//Warning!!  This method accepts the RA in decimal form and then will convert it to degrees for Astrometry.net
void SexySolver::setSearchPositionRaDec(double ra, double dec)
{
    setSearchPositionInDegrees(ra * 15.0, dec);
}

//This is a convenience function used to set all the search position parameters based on the ra, dec, and radius
//Warning!!  This method accepts the RA in degrees just like the DEC
void SexySolver::setSearchPositionInDegrees(double ra, double dec)
{
    use_position = true;
    //3
    search_ra = ra;
    //4
    search_dec = dec;
}

void addPathToListIfExists(QStringList *list, QString path)
{
    if(list)
    {
        if(QFileInfo(path).exists())
            list->append(path);
    }
}

QStringList SexySolver::getDefaultIndexFolderPaths()
{
    QStringList indexFilePaths;
#if defined(Q_OS_OSX)
    //Mac Default location
    addPathToListIfExists(&indexFilePaths, QDir::homePath() + "/Library/Application Support/Astrometry");
    //Homebrew location
    addPathToListIfExists(&indexFilePaths, "/usr/local/share/astrometry");
#elif defined(Q_OS_LINUX)
    //Linux Default Location
    addPathToListIfExists(&indexFilePaths, "/usr/share/astrometry/");
    //Linux Local KStars Location
    addPathToListIfExists(&indexFilePaths, QDir::homePath() + "/.local/share/kstars/astrometry/");
#elif defined(_WIN32)
    //Windows Locations
    addPathToListIfExists(&indexFilePaths, QDir::homePath() + "/AppData/Local/cygwin_ansvr/usr/share/astrometry/data");
    addPathToListIfExists(&indexFilePaths, "C:/cygwin/usr/share/astrometry/data");
#endif
    return indexFilePaths;
}



wcs_point * SexySolver::getWCSCoord()
{
    if(hasWCS)
        return wcs_coord;
    else
        return nullptr;
}

QList<Star> SexySolver::appendStarsRAandDEC(QList<Star> stars)
{

    return sextractorSolver->appendStarsRAandDEC(stars);
}

QMap<QString, QVariant> SexySolver::convertToMap(Parameters params)
{
    QMap<QString, QVariant> settingsMap;
    settingsMap.insert("listName", QVariant(params.listName));

    //These are to pass the parameters to the internal sextractor
    settingsMap.insert("apertureShape", QVariant(params.apertureShape));
    settingsMap.insert("kron_fact", QVariant(params.kron_fact));
    settingsMap.insert("subpix", QVariant(params.subpix));
    settingsMap.insert("r_min", QVariant(params.r_min));
    //params.inflags
    settingsMap.insert("magzero", QVariant(params.magzero));
    settingsMap.insert("minarea", QVariant(params.minarea));
    settingsMap.insert("deblend_thresh", QVariant(params.deblend_thresh));
    settingsMap.insert("deblend_contrast", QVariant(params.deblend_contrast));
    settingsMap.insert("clean", QVariant(params.clean));
    settingsMap.insert("clean_param", QVariant(params.clean_param));

    settingsMap.insert("fwhm", QVariant(params.fwhm));
    QStringList conv;
    foreach(float num, params.convFilter)
    {
        conv << QVariant(num).toString();
    }
    settingsMap.insert("convFilter", QVariant(conv.join(",")));

    //Star Filter Settings
    settingsMap.insert("maxSize", QVariant(params.maxSize));
    settingsMap.insert("minSize", QVariant(params.minSize));
    settingsMap.insert("maxEllipse", QVariant(params.maxEllipse));
    settingsMap.insert("keepNum", QVariant(params.keepNum));
    settingsMap.insert("removeBrightest", QVariant(params.removeBrightest));
    settingsMap.insert("removeDimmest", QVariant(params.removeDimmest ));
    settingsMap.insert("saturationLimit", QVariant(params.saturationLimit));

    //Settings that usually get set by the Astrometry config file
    settingsMap.insert("maxwidth", QVariant(params.maxwidth)) ;
    settingsMap.insert("minwidth", QVariant(params.minwidth)) ;
    settingsMap.insert("inParallel", QVariant(params.inParallel)) ;
    settingsMap.insert("multiAlgo", QVariant(params.multiAlgorithm)) ;
    settingsMap.insert("solverTimeLimit", QVariant(params.solverTimeLimit));

    //Astrometry Basic Parameters
    settingsMap.insert("resort", QVariant(params.resort)) ;
    settingsMap.insert("downsample", QVariant(params.downsample)) ;
    settingsMap.insert("search_radius", QVariant(params.search_radius)) ;

    //Setting the settings to know when to stop or keep searching for solutions
    settingsMap.insert("logratio_tokeep", QVariant(params.logratio_tokeep)) ;
    settingsMap.insert("logratio_totune", QVariant(params.logratio_totune)) ;
    settingsMap.insert("logratio_tosolve", QVariant(params.logratio_tosolve)) ;

    return settingsMap;

}

Parameters SexySolver::convertFromMap(QMap<QString, QVariant> settingsMap)
{
    Parameters params;
    params.listName = settingsMap.value("listName", params.listName).toString();

    //These are to pass the parameters to the internal sextractor

    params.apertureShape = (Shape)settingsMap.value("apertureShape", params.listName).toInt();
    params.kron_fact = settingsMap.value("kron_fact", params.listName).toDouble();
    params.subpix = settingsMap.value("subpix", params.listName).toInt();
    params.r_min= settingsMap.value("r_min", params.listName).toDouble();
    //params.inflags
    params.magzero = settingsMap.value("magzero", params.magzero).toDouble();
    params.minarea = settingsMap.value("minarea", params.minarea).toDouble();
    params.deblend_thresh = settingsMap.value("deblend_thresh", params.deblend_thresh).toInt();
    params.deblend_contrast = settingsMap.value("deblend_contrast", params.deblend_contrast).toDouble();
    params.clean = settingsMap.value("clean", params.clean).toInt();
    params.clean_param = settingsMap.value("clean_param", params.clean_param).toDouble();

    //The Conv Filter
    params.fwhm = settingsMap.value("fwhm",params.fwhm).toDouble();
    if(settingsMap.contains("convFilter"))
    {
        QStringList conv = settingsMap.value("convFilter", "").toString().split(",");
        QVector<float> filter;
        foreach(QString item, conv)
            filter.append(QVariant(item).toFloat());
        params.convFilter = filter;
    }

    //Star Filter Settings
    params.maxSize = settingsMap.value("maxSize", params.maxSize).toDouble();
    params.minSize = settingsMap.value("minSize", params.minSize).toDouble();
    params.maxEllipse = settingsMap.value("maxEllipse", params.maxEllipse).toDouble();
    params.keepNum = settingsMap.value("keepNum", params.keepNum).toDouble();
    params.removeBrightest = settingsMap.value("removeBrightest", params.removeBrightest).toDouble();
    params.removeDimmest = settingsMap.value("removeDimmest", params.removeDimmest ).toDouble();
    params.saturationLimit = settingsMap.value("saturationLimit", params.saturationLimit).toDouble();

    //Settings that usually get set by the Astrometry config file
    params.maxwidth = settingsMap.value("maxwidth", params.maxwidth).toDouble() ;
    params.minwidth = settingsMap.value("minwidth", params.minwidth).toDouble() ;
    params.inParallel = settingsMap.value("inParallel", params.inParallel).toBool() ;
    params.multiAlgorithm = (MultiAlgo)(settingsMap.value("multiAlgo", params.multiAlgorithm)).toInt();
    params.solverTimeLimit = settingsMap.value("solverTimeLimit", params.solverTimeLimit).toInt();

    //Astrometry Basic Parameters
    params.resort = settingsMap.value("resort", params.resort).toBool();
    params.downsample = settingsMap.value("downsample", params.downsample).toBool() ;
    params.search_radius = settingsMap.value("search_radius", params.search_radius).toDouble() ;

    //Setting the settings to know when to stop or keep searching for solutions
    params.logratio_tokeep = settingsMap.value("logratio_tokeep", params.logratio_tokeep).toDouble() ;
    params.logratio_totune = settingsMap.value("logratio_totune", params.logratio_totune).toDouble() ;
    params.logratio_tosolve = settingsMap.value("logratio_tosolve", params.logratio_tosolve).toDouble();

    return params;

}

//This function should get the system RAM in bytes.  I may revise it later to get the currently available RAM
//But from what I read, getting the Available RAM is inconsistent and buggy on many systems.
uint64_t SexySolver::getAvailableRAM()
{
    uint64_t RAM = 0;

#if defined(Q_OS_OSX)
    int mib[2];
    size_t length;
    mib[0] = CTL_HW;
    mib[1] = HW_MEMSIZE;
    length = sizeof(int64_t);
    if(sysctl(mib, 2, &RAM, &length, NULL, 0))
        return 0; // On Error
#elif defined(Q_OS_LINUX)
    QProcess p;
    p.start("awk", QStringList() << "/MemTotal/ { print $2 }" << "/proc/meminfo");
    p.waitForFinished();
    QString memory = p.readAllStandardOutput();
    RAM = memory.toLong() * 1024; //It is in kB on this system
    p.close();
#else
    MEMORYSTATUSEX memory_status;
    ZeroMemory(&memory_status, sizeof(MEMORYSTATUSEX));
    memory_status.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memory_status)) {
      RAM = memory_status.ullTotalPhys;
    } else {
      RAM = 0;
    }
#endif
    return RAM;
}

//This should determine if enough RAM is available to load all the index files in parallel
bool SexySolver::enoughRAMisAvailableFor(QStringList indexFolders)
{
    uint64_t totalSize = 0;

    foreach(QString folder, indexFolders)
    {
        QDir dir(folder);
        if(dir.exists())
        {
            dir.setNameFilters(QStringList()<<"*.fits"<<"*.fit");
            QFileInfoList indexInfoList = dir.entryInfoList();
            foreach(QFileInfo indexInfo, indexInfoList)
                totalSize += indexInfo.size();
        }

    }
    uint64_t availableRAM = getAvailableRAM();
    if(availableRAM == 0)
    {
        if(logLevel != LOG_NONE)
            emit logOutput("Unable to determine system RAM for inParallel Option");
        return false;
    }
    float bytesInGB = 1024 * 1024 * 1024; // B -> KB -> MB -> GB , float to make sure it reports the answer with any decimals
    if(logLevel != LOG_NONE)
        emit logOutput(QString("Evaluating Installed RAM for inParallel Option.  Total Size of Index files: %1 GB, Installed RAM: %2 GB").arg(totalSize / bytesInGB).arg(availableRAM / bytesInGB));
    return availableRAM > totalSize;
}





