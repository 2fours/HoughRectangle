#include "process_image.hpp"
#include <math.h>
#include <stdlib.h>
#include <Eigen/Dense>
#include <iostream>
#include "config.hpp"
#include "io.hpp"
#include "stb_image.h"
#include "stb_image_write.h"
#include <tuple>

#define PI 3.14159265

using namespace Eigen;

/*************************************************************************************/
void Log(const char* message) { std::cout << message << std::endl; }

/*************************************************************************************/
void normalise_img(Matrix<float, Dynamic, Dynamic, RowMajor>& img) {
    Matrix<float, Dynamic, Dynamic, RowMajor> high;
    high.setOnes(img.rows(), img.cols());
    high *= 255;

    Matrix<float, Dynamic, Dynamic, RowMajor> low;
    low.setZero(img.rows(), img.cols());

    Matrix<float, Dynamic, Dynamic, RowMajor> tmp =
        (img.array() > 128.0).select(high, low);

    img = tmp;
}

// TODO(luczeng): maybe give credit to the original author ;)
/*************************************************************************************/
std::vector<float> LinearSpacedArray(float a, float b, std::size_t N) {
    double h = (b - a) / static_cast<float>(N - 1);
    std::vector<float> xs(N);
    std::vector<float>::iterator x;
    float val;
    for (x = xs.begin(), val = a; x != xs.end(); ++x, val += h) {
        *x = val;
    }
    return xs;
}

/*************************************************************************************/
std::vector<Index> find_local_maximum(
    Matrix<float, Dynamic, Dynamic, RowMajor>& hough, float threshold) {
    std::vector<Index> idxs;

    // This loop can probably be replaced by something faster(factorized?)
    // TODO(luczeng): what about std::max_element?
    for (Index i = 0; i < hough.size(); ++i) {
        if (hough(i) >= threshold) idxs.push_back(i);
    }

    return idxs;
}

/*************************************************************************************/
HoughRectangle::HoughRectangle(HoughRectangle::fMat & img, int thetaBins, int rhoBins,
    float thetaMin, float thetaMax) {
    m_img = img;
    m_thetaBins = thetaBins;
    m_thetaMin = thetaMin;
    m_thetaMax = thetaMax;
    m_rhoBins = rhoBins;

    VectorXf m_theta_vec =
        VectorXf::LinSpaced(Sequential, thetaBins, thetaMin, thetaMax);

    std::vector<float> m_rho_vec = LinearSpacedArray(
        -sqrt(pow(img.rows() / 2.0, 2) + pow(img.rows() / 2.0, 2)),
        sqrt(pow(img.rows() / 2.0, 2) + pow(img.rows() / 2.0, 2)), rhoBins);
}

/*************************************************************************************/
HoughRectangle::fMat HoughRectangle::ring(
    HoughRectangle::fMat & img, int r_min, int r_max) {
    HoughRectangle::fMat result = img.replicate<1, 1>();
    float center_x, center_y;
    if (remainder(img.cols(), 2) != 0) {
        center_x = (img.cols() - 1) / 2;
    } else {
        center_x = (img.cols() / 2);
    }
    if (remainder(img.rows(), 2) != 0) {
        center_y = (img.rows() - 1) / 2;
    } else {
        center_y = (img.rows() - 1) / 2;
    }

    for (int i = 0; i < img.cols(); ++i) {
        for (int j = 0; j < img.rows(); ++j) {
            float dist = sqrt(pow(i - center_x, 2) + pow(j - center_y, 2));
            if (dist < r_min or dist > r_max) {
                result(j, i) = 0;
            }
        }
    }

    return result;
}

/*************************************************************************************/
HoughRectangle::fMat HoughRectangle::windowed_hough(
    HoughRectangle::fMat& img, int r_min, int r_max){

    HoughRectangle::fMat ringed_subregion =
        ring(img, r_min, r_max);

    HoughRectangle::fMat wht = hough_transform(
        ringed_subregion);

    return wht;
}

/*************************************************************************************/
HoughRectangle::fMat HoughRectangle::apply_windowed_hough(
    fMat& img, int L_window, int r_min,
    int r_max){
    for (int i = 0; i < img.rows() - L_window; ++i) {
        for (int j = 0; j < img.cols() - L_window; ++j) {
            // Applying circular mask to local region
            // TODO(luczeng): This is weird. You keep overwriting a local variable.
            Matrix<float, Dynamic, Dynamic, RowMajor> subregion =
                img.block(i, j, L_window, L_window);
        }
    }
    // TODO(luczeng): return something?
    return HoughRectangle::fMat{};
}

/*************************************************************************************/
HoughRectangle::fMat HoughRectangle::hough_transform(
    fMat& img) {

    // Define accumulator matrix, theta and rho vectors
    // TODO(luczeng): please give your users the freedom to pass in their self-allocated matrix. They might want to reuse memory and thus save a lot of time (if fMat is big).
    HoughRectangle::fMat acc =
        MatrixXf::Zero(m_rhoBins, m_thetaBins);  // accumulator


    // Cartesian coordinate vectors
    VectorXi vecX =
        VectorXi::LinSpaced(Sequential, img.cols(), 0, img.cols() - 1);
    VectorXi vecY =
        VectorXi::LinSpaced(Sequential, img.rows(), 0, img.rows() - 1);
    int mid_X = round(img.cols() / 2);
    int mid_Y = round(img.rows() / 2);
    vecX = vecX.array() - mid_X;
    vecY = vecY.array() - mid_Y;

    // Pre-compute cosines and sinuses:
    VectorXf cosT = cos(m_theta_vec.array() * PI / 180.0);
    VectorXf sinT = sin(m_theta_vec.array() * PI / 180.0);

    // Compute Hough transform
    for (int i = 0; i < img.rows(); ++i) {
        for (int j = 0; j < img.cols(); ++j) {
            if (img(i, j) != 0) {
                // generate sinusoidal curve
                for (int k = 0; k < m_theta_vec.size(); ++k) {
                    // Calculate rho value
                    float rho_tmp = (vecX[j] * cosT[k] + vecY[i] * sinT[k]);

                    std::vector<float>::iterator idx;
                    idx = std::lower_bound(m_rho_vec.begin(), m_rho_vec.end(), rho_tmp);
                    int idx_rho = idx - m_rho_vec.begin() - 1;
                    // std::cout <<rho_tmp<<std::endl;
                    if (idx_rho < 0) {
                        idx_rho = 0;
                    }

                    // Fill accumulator
                    acc(idx_rho, k) = acc(idx_rho, k) + 1;
                    if (acc(idx_rho, k) > pow(2, 32)) {
                        std::cout << "Max value overpassed";
                    }
                }
            }
        }
    }

    // Enhanced HT

    return acc;
}

/*************************************************************************************/
HoughRectangle::fMat HoughRectangle::enhance_hough(
    HoughRectangle::fMat & hough, int h, int w) {
    HoughRectangle::fMat houghpp =
        MatrixXf::Zero(hough.rows(), hough.cols());

    for (int i = h; i < hough.rows() - h; ++i) {
        for (int j = w; j < hough.cols() - w; ++j) {
            /*           double tmp =
             * pow(hough(i,j)/sqrt(hough.block(i-h/2,j-w/2,h,w).sum()),2);*/
            /*std::cout <<tmp<<" "<<hough(i,j)<<" "<<
             * hough.block(i-h/2,j-w/2,h,w).sum()<<std::endl;*/
            if (hough.block(i - h / 2, j - w / 2, h, w).sum() == 0) {
                houghpp(i, j) = 0;
            } else {
                houghpp(i, j) = pow(hough(i, j), 2) * h * w /
                                hough.block(i - h / 2, j - w / 2, h, w).sum();
            }
        }
    }

    return houghpp;
}

