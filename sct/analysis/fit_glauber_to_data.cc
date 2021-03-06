/* Scans through a 3D grid of parameters (Npp, k, x) and builds up  a refmult
 * distribution by sampling the npart vs ncoll distribution from the glauber
 * results N times for every set of parameters. Compares the generated refmult
 * distribution to a data refmult distribution using a chi2 similarity measure.
 * Reports the best fit parameters.
 *
 */

#include "sct/centrality/centrality.h"
#include "sct/centrality/nbd_fit.h"
#include "sct/lib/enumerations.h"
#include "sct/lib/flags.h"
#include "sct/lib/logging.h"
#include "sct/lib/string/string_cast.h"
#include "sct/lib/string/string_utils.h"
#include "sct/utils/random.h"

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "boost/filesystem.hpp"

#include "TCanvas.h"
#include "TError.h"
#include "TFile.h"
#include "TH1D.h"
#include "TH2D.h"
#include "TROOT.h"
#include "TTree.h"
#include "TTreeReader.h"
#include "TTreeReaderValue.h"

// program settings
SCT_DEFINE_string(outDir, "tmp",
                  "Path to directory to store output ROOT files");
SCT_DEFINE_string(outFile, "fit_results", "output file name (no extension)");
SCT_DEFINE_bool(saveAll, false,
                "save all fit histograms: if false, only saves best fit");
SCT_DEFINE_string(
    glauberFile, "npartncoll.root",
    "path to root file containing the npart x ncoll distribution");
SCT_DEFINE_string(glauberHistName, "npartncoll",
                  "name of npart x ncoll histogram");
SCT_DEFINE_string(dataFile, "refmult.root",
                  "path to root file containing data refmult distribution");
SCT_DEFINE_string(dataHistName, "refmult",
                  "name of reference multiplicty histogram");
SCT_DEFINE_int(events, 1e5, "number of events per fit");

// model settings
SCT_DEFINE_double(npp_min, 1.0, "minimum Npp for negative binomial");
SCT_DEFINE_double(npp_max, 4.0, "maximum Npp for negative binomial");
SCT_DEFINE_int(npp_steps, 31, "number of steps in Npp range to sample");
SCT_DEFINE_double(k_min, 1.0, "minimum k for negative binomial");
SCT_DEFINE_double(k_max, 4.0, "maximum k for negative binomial");
SCT_DEFINE_int(k_steps, 31, "number of steps in k range to sample");
SCT_DEFINE_double(x_min, 0.1, "minimum x for two component multiplicity");
SCT_DEFINE_double(x_max, 0.4, "maximum x for two component multiplicity");
SCT_DEFINE_int(x_steps, 31, "number of steps in x range to sample");
SCT_DEFINE_double(ppEfficiency, 0.98, "pp efficiency");
SCT_DEFINE_double(AuAuEfficiency, 0.84, "0-5% central AuAu efficiency");
SCT_DEFINE_int(centMult, 540, "average 0-5% central multiplicity");
SCT_DEFINE_bool(constEff, false, "turn on to use only pp efficiency");
SCT_DEFINE_bool(useStGlauberChi2, true,
                "use StGlauber Chi2 calculation instead of ROOT");
SCT_DEFINE_bool(useStGlauberNorm, true,
                "use StGlauber Normalization instead of integral norm");
SCT_DEFINE_double(trigBias, 1.0, "trigger bias");
SCT_DEFINE_int(minMult, 100,
               "minimum multiplicity for chi2 comparisons in fit");
SCT_DEFINE_int(seed, 252452, "seed for sct RNG");

int main(int argc, char *argv[]) {
  // shut ROOT up :)
  gErrorIgnoreLevel = kWarning;

  // set help message
  std::string usage =
      "Performs a grid search over the multiplicity model parameter space: ";
  usage += "[Npp, k, x, pp efficiency, central AuAu efficiency, trigger "
           "efficiency], using a chi2 ";
  usage += "fit to a measured refmult distribution as the objective function.";
  sct::SetUsageMessage(usage);

  sct::InitLogging(&argc, argv);
  sct::ParseCommandLineFlags(&argc, argv);

  // build output directory if it doesn't exist, using boost::filesystem
  boost::filesystem::path dir(FLAGS_outDir);
  boost::filesystem::create_directories(dir);

  // load the input files data refmult histogram & glauber npart x ncoll
  TFile *glauber_file = new TFile(FLAGS_glauberFile.c_str(), "READ");
  TFile *data_file = new TFile(FLAGS_dataFile.c_str(), "READ");

  if (!glauber_file->IsOpen()) {
    LOG(ERROR) << "Glauber input file could not be opened: "
               << FLAGS_glauberFile << " not found or corrupt";
    return 1;
  }
  if (!data_file->IsOpen()) {
    LOG(ERROR) << "Data input file could not be opened: " << FLAGS_dataFile
               << " not found or corrupt";
    return 1;
  }

  // load data refmult histogram & glauber npart x ncoll
  TH2D *npartncoll = (TH2D *)glauber_file->Get(FLAGS_glauberHistName.c_str());
  TH1D *refmult = (TH1D *)data_file->Get(FLAGS_dataHistName.c_str());

  if (npartncoll == nullptr) {
    LOG(ERROR) << "NPart x NColl histogram: " << FLAGS_glauberHistName
               << " not found in file: " << FLAGS_glauberFile;
    return 1;
  }
  if (refmult == nullptr) {
    LOG(ERROR) << "Reference multiplicity histogram: " << FLAGS_dataHistName
               << " not found in file: " << FLAGS_dataFile;
    return 1;
  }

  // create our fitting model
  sct::NBDFit fitter(refmult, npartncoll);
  fitter.minimumMultiplicityCut(FLAGS_minMult);
  fitter.useStGlauberChi2(FLAGS_useStGlauberChi2);
  fitter.useStGlauberNorm(FLAGS_useStGlauberNorm);
  sct::Random::instance().seed(FLAGS_seed);

  // scan
  auto results = fitter.scan(
      FLAGS_events, FLAGS_npp_steps, FLAGS_npp_min, FLAGS_npp_max,
      FLAGS_k_steps, FLAGS_k_min, FLAGS_k_max, FLAGS_x_steps, FLAGS_x_min,
      FLAGS_x_max, FLAGS_ppEfficiency, FLAGS_AuAuEfficiency, FLAGS_centMult,
      FLAGS_trigBias, FLAGS_constEff, FLAGS_saveAll);

  // find best fit
  double best_chi2 = 9999;
  std::string best_key;

  // and we will parse the parameters
  double x = 0.0;
  double npp = 0.0;
  double k = 0.0;

  for (auto &result : results) {
    if (result.second->chi2 / result.second->ndf < best_chi2) {
      best_chi2 = result.second->chi2 / result.second->ndf;
      best_key = result.first;

      // parse the parameters
      std::vector<std::string> split;
      sct::SplitString(best_key, split, '_');
      npp = strtof(split[1].c_str(), 0);
      k = strtof(split[3].c_str(), 0);
      x = strtof(split[5].c_str(), 0);
    }
  }

  LOG(INFO) << "BEST FIT: " << best_key;
  LOG(INFO) << "chi2/ndf: " << best_chi2;

  // now we will generate a new simulation curve using the fitter,
  // but we will use greater statistics
  fitter.setParameters(npp, k, x, FLAGS_ppEfficiency, FLAGS_AuAuEfficiency,
                       FLAGS_centMult, FLAGS_trigBias, FLAGS_constEff);
  auto refit = fitter.fit(1e6);
  LOG(INFO) << "finished fitting";

  // and we save the results to disk
  std::string output_name = FLAGS_outDir + "/" + FLAGS_outFile + ".root";
  TFile out(output_name.c_str(), "RECREATE");
  refit->data->SetName("refmult");
  refit->data->Write();
  refit->simu->SetNameTitle("glauber", best_key.c_str());
  refit->simu->SetTitle(best_key.c_str());
  refit->simu->Write();

  TH1D* ratio = (TH1D*) refit->simu->Clone("ratio");
  ratio->Divide(refit->data);
  ratio->Write();

  // write all histograms to disk if they were saved
  if (FLAGS_saveAll) {
    for (auto &result : results) {
      double chi2 = result.second->chi2 / result.second->ndf;
      std::string name = result.first;
      std::string title =
          result.first + "_chi2/ndf=" +
          sct::MakeString(std::fixed, std::setprecision(5), chi2);
      result.second->simu->SetNameTitle(name.c_str(), title.c_str());
      result.second->simu->Write();
    }
  }

  out.Close();

  gflags::ShutDownCommandLineFlags();
  return 0;
}

