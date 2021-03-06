// This file is part of the AliceVision project.
// Copyright (c) 2016 AliceVision contributors.
// Copyright (c) 2012 openMVG contributors.
// This Source Code Form is subject to the terms of the Mozilla Public License,
// v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "generateReport.hpp"

#include <dependencies/htmlDoc/htmlDoc.hpp>
#include <dependencies/histogram/histogram.hpp>
#include <dependencies/vectorGraphics/svgDrawer.hpp>

#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;

namespace aliceVision {
namespace sfm {

bool Generate_SfM_Report(const SfMData & sfm_data,
                         const std::string & htmlFilename)
{
  // Compute mean,max,median residual values per View
  IndexT residualCount = 0;
  HashMap< IndexT, std::vector<double> > residuals_per_view;
  for (Landmarks::const_iterator iterTracks = sfm_data.GetLandmarks().begin();
    iterTracks != sfm_data.GetLandmarks().end();
    ++iterTracks
  )
  {
    const Observations & observations = iterTracks->second.observations;
    for (Observations::const_iterator itObs = observations.begin();
      itObs != observations.end(); ++itObs)
    {
      const View * view = sfm_data.GetViews().at(itObs->first).get();
      const geometry::Pose3 pose = sfm_data.getPose(*view);
      const camera::IntrinsicBase * intrinsic = sfm_data.GetIntrinsics().at(view->getIntrinsicId()).get();
      // Use absolute values
      const Vec2 residual = intrinsic->residual(pose, iterTracks->second.X, itObs->second.x).array().abs();
      residuals_per_view[itObs->first].push_back(residual(0));
      residuals_per_view[itObs->first].push_back(residual(1));
      ++residualCount;
    }
  }
  using namespace htmlDocument;
  // extract directory from htmlFilename
  const std::string sTableBegin = "<table border=\"1\">",
    sTableEnd = "</table>",
    sRowBegin= "<tr>", sRowEnd = "</tr>",
    sColBegin = "<td>", sColEnd = "</td>",
    sNewLine = "<br>", sFullLine = "<hr>";

  htmlDocument::htmlDocumentStream htmlDocStream("[report] SfM reconstruction");
  htmlDocStream.pushInfo(
  htmlDocument::htmlMarkup("h1", std::string("[report] SfM reconstruction")));
  htmlDocStream.pushInfo(sFullLine);

  htmlDocStream.pushInfo( "Dataset info:" + sNewLine );

  std::ostringstream os;
  os << "#views: " << sfm_data.GetViews().size() << sNewLine
  << " #valid views: " << sfm_data.getValidViews().size() << sNewLine
  << " #poses: " << sfm_data.GetPoses().size() << sNewLine
  << " #intrinsics: " << sfm_data.GetIntrinsics().size() << sNewLine
  << " #tracks: " << sfm_data.GetLandmarks().size() << sNewLine
  << " #residuals: " << residualCount << sNewLine;

  htmlDocStream.pushInfo( os.str() );
  htmlDocStream.pushInfo( sFullLine );

  htmlDocStream.pushInfo( sTableBegin);
  os.str("");
  os << sRowBegin
    << sColBegin + "IdView" + sColEnd
    << sColBegin + "Basename" + sColEnd
    << sColBegin + "#Observations" + sColEnd
    << sColBegin + "Residuals min" + sColEnd
    << sColBegin + "Residuals median" + sColEnd
    << sColBegin + "Residuals mean" + sColEnd
    << sColBegin + "Residuals max" + sColEnd
    << sRowEnd;
  htmlDocStream.pushInfo( os.str() );

  for (Views::const_iterator iterV = sfm_data.GetViews().begin();
    iterV != sfm_data.GetViews().end();
    ++iterV)
  {
    const View * v = iterV->second.get();
    const IndexT id_view = v->getViewId();

    os.str("");
    os << sRowBegin
      << sColBegin << id_view << sColEnd
      << sColBegin + fs::path(v->getImagePath()).stem().string() + sColEnd;

    // IdView | basename | #Observations | residuals min | residual median | residual max
    if (sfm_data.IsPoseAndIntrinsicDefined(v))
    {
      if( residuals_per_view.find(id_view) != residuals_per_view.end() )
      {
        const std::vector<double> & residuals = residuals_per_view.at(id_view);
        if (!residuals.empty())
        {
          MinMaxMeanMedian<double> stats(residuals.begin(), residuals.end());
          os << sColBegin << residuals.size()/2 << sColEnd // #observations
            << sColBegin << stats.min << sColEnd
            << sColBegin << stats.median << sColEnd
            << sColBegin << stats.mean << sColEnd
            << sColBegin << stats.max <<sColEnd;
        }
      }
    }
    os << sRowEnd;
    htmlDocStream.pushInfo( os.str() );
  }
  htmlDocStream.pushInfo( sTableEnd );
  htmlDocStream.pushInfo( sFullLine );

  // combine all residual values into one vector
  // export the SVG histogram
  {
    IndexT residualCount = 0;
    for (HashMap< IndexT, std::vector<double> >::const_iterator
      it = residuals_per_view.begin();
      it != residuals_per_view.end();
      ++it)
    {
      residualCount += it->second.size();
    }
    // Concat per view residual values into one vector
    std::vector<double> residuals(residualCount);
    residualCount = 0;
    for (HashMap< IndexT, std::vector<double> >::const_iterator
      it = residuals_per_view.begin();
      it != residuals_per_view.end();
      ++it)
    {
      std::copy(it->second.begin(),
        it->second.begin()+it->second.size(),
        residuals.begin()+residualCount);
      residualCount += it->second.size();
    }
    if (!residuals.empty())
    {
      // RMSE computation
      const Eigen::Map<Eigen::RowVectorXd> residuals_mapping(&residuals[0], residuals.size());
      const double RMSE = std::sqrt(residuals_mapping.squaredNorm() / (double)residuals.size());
      os.str("");
      os << sFullLine << "SfM Scene RMSE: " << RMSE << sFullLine;
      htmlDocStream.pushInfo(os.str());

      const double maxRange = *max_element(residuals.begin(), residuals.end());
      Histogram<double> histo(0.0, maxRange, 100);
      histo.Add(residuals.begin(), residuals.end());

      svg::svgHisto svg_Histo;
      svg_Histo.draw(histo.GetHist(), std::pair<float,float>(0.f, maxRange),
        (fs::path(htmlFilename).parent_path() / std::string("residuals_histogram.svg")).string(),
        600, 200);

      os.str("");
      os << sNewLine<< "Residuals histogram" << sNewLine;
      os << "<img src=\""
        << "residuals_histogram.svg"
        << "\" height=\"300\" width =\"800\">\n";
      htmlDocStream.pushInfo(os.str());
    }
  }

  std::ofstream htmlFileStream(htmlFilename.c_str());
  htmlFileStream << htmlDocStream.getDoc();
  const bool bOk = !htmlFileStream.bad();
  return bOk;
}

} // namespace sfm
} // namespace aliceVision
