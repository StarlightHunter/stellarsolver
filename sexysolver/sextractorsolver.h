/*  SextractorSolver, SexySolver Intenal Library developed by Robert Lancaster, 2020

    This application is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.
*/
#ifndef SEXTRACTORSOLVER_H
#define SEXTRACTORSOLVER_H

//Includes for this project
#include <QThread>
#include <QRect>
#include <QDir>
#include "structuredefinitions.h"

//CFitsio Includes
#include "fitsio.h"

//Astrometry.net includes
extern "C"{
#include "astrometry/blindutils.h"
#include "astrometry/engine.h"
#include "astrometry/sip-utils.h"
}

class SextractorSolver : public QThread
{
    Q_OBJECT
public:
    SextractorSolver(ProcessType type, Statistic imagestats,  const uint8_t *imageBuffer, QObject *parent = nullptr);
    ~SextractorSolver();

    ProcessType processType;
    virtual int sextract()=0;
    //virtual void solve()=0;
    //These are the most important methods that you can use for the SexySolver
    virtual void executeProcess();                      //This runs the process without threading.
    virtual void startProcess();                        //This starts the process in a separate thread
    virtual void abort() = 0;
    virtual SextractorSolver* spawnChildSolver(int n) = 0;
    //This will abort the solver

    virtual wcs_point *getWCSCoord() = 0;
    virtual QList<Star> appendStarsRAandDEC(QList<Star> stars) = 0;

    //Logging Settings for Astrometry
    bool logToFile = false;             //This determines whether or not to save the output from Astrometry.net to a file
    QString logFileName;                //This is the path to the log file that it will save.
    log_level logLevel = LOG_MSG;       //This is the level of logging getting saved to the log file

    //These are for creating temporary files
    QString baseName;                   //This is the base name used for all temporary files.  It uses a random name based on the type of solver/sextractor.
    QString basePath;                   //This is the path used for saving any temporary files.  They are by default saved to the default temp directory, you can change it if you want to.

    Parameters params;                  //The currently set parameters for SexySolver
    QStringList indexFolderPaths;       //This is the list of folder paths that the solver will use to search for index files

    //Astrometry Scale Parameters, These are not saved parameters and change for each image, use the methods to set them
    bool use_scale = false;             //Whether or not to use the image scale parameters
    double scalelo = 0;                 //Lower bound of image scale estimate
    double scalehi = 0;                 //Upper bound of image scale estimate
    ScaleUnits scaleunit;               //In what units are the lower and upper bounds?

    //Astrometry Position Parameters, These are not saved parameters and change for each image, use the methods to set them
    bool use_position = false;          //Whether or not to use initial information about the position
    double search_ra = HUGE_VAL;        //RA of field center for search, format: decimal degrees
    double search_dec = HUGE_VAL;       //DEC of field center for search, format: decimal degrees

    QString getScaleUnitString()
    {
        switch(scaleunit)
        {
            case DEG_WIDTH:
                return "degwidth";
                break;
            case ARCMIN_WIDTH:
                return "arcminwidth";
                break;
            case ARCSEC_PER_PIX:
                return "arcsecperpix";
                break;
            case FOCAL_MM:
                return "focalmm";
            break;
            default: return ""; break;
        }
    }

    void setSearchScale(double fov_low, double fov_high, ScaleUnits units); //This sets the scale range for the image to speed up the solver                                                    //This sets the search RA/DEC/Radius to speed up the solver
    void setSearchPositionInDegrees(double ra, double dec);
    int depthlo = -1;                       //This is the low depth of this child solver
    int depthhi = -1;                       //This is the high depth of this child solver


    Background getBackground(){return background;}
    int getNumStarsFound(){return stars.size();};
    QList<Star> getStarList(){return stars;}
    Solution getSolution(){return solution;};
    bool hasWCSData(){return hasWCS;};
    bool solvingDone(){return hasSolved;};
    bool isCalculatingHFR(){return calculateHFR;};
    void setUseSubframe(QRect frame){useSubframe = true; subframe = frame;};

protected:  //Note: These items are not private because they are needed by ExternalSextractorSolver

    bool useSubframe = false;
    QRect subframe;
    //SexySolver Internal settings that are needed by ExternalSextractorSolver as well
    bool calculateHFR = false;          //Whether or not the HFR of the image should be calculated using sep_flux_radius.  Don't do it unless you need HFR
    bool hasSextracted = false;         //This boolean is set when the sextraction is done
    bool hasSolved = false;             //This boolean is set when the solving is done
    Statistic stats;                    //This is information about the image
    const uint8_t *m_ImageBuffer { nullptr }; //The generic data buffer containing the image data
    bool usingDownsampledImage = false; //This boolean gets set internally if we are using a downsampled image buffer for SEP

    //The Results
    Background background;      //This is a report on the background levels found during sextraction
    QList<Star> stars;          //This is the list of stars that get sextracted from the image, saved to the file, and then solved by astrometry.net
    Solution solution;          //This is the solution that comes back from the Solver
    bool runSEPSextractor();    //This is the method that actually runs the internal sextractor
    bool hasWCS = false;        //This boolean gets set if the SexySolver has WCS data to retrieve

    bool wasAborted = false;
    // This is the cancel file path that astrometry.net monitors.  If it detects this file, it aborts the solve
    QString cancelfn;           //Filename whose creation signals the process to stop
    QString solvedfn;           //Filename whose creation tells astrometry.net it already solved the field.

    bool isChildSolver = false;              //This identifies that this solver is in fact a child solver.

    double convertToDegreeHeight(double scale){
        switch(scaleunit)
        {
            case DEG_WIDTH:
                return scale;
                break;
            case ARCMIN_WIDTH:
                return arcmin2deg(scale);
                break;
            case ARCSEC_PER_PIX:
                return arcsec2deg(scale) * (double)stats.height;
                break;
            case FOCAL_MM:
                return rad2deg(atan(36. / (2. * scale)));
            break;
            default: return scale; break;
        }
    }

signals:

    //This signals that there is infomation that should be printed to a log file or log window
    void logOutput(QString logText);

    //This signals that the sextraction or image solving is complete, whether they were successful or not
    //A -1 or some positive value should signify failure, where a 0 should signify success.
    void finished(int x);

};

#endif // SEXTRACTORSOLVER_H
